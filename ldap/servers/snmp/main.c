/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <ldap-agent.h>

static char *agentx_master = NULL;
static char *agent_logdir = NULL;
static char *pidfile = NULL;
server_instance *server_head = NULL;

static int keep_running;

RETSIGTYPE
stop_server(int signum) {
    if (signum == SIGUSR1) {
        snmp_log(LOG_INFO, "Detected attempt to start ldap-agent again.\n");
    } else {
        snmp_log(LOG_INFO, "Received stop signal.  Stopping ldap-agent...\n");
        keep_running = 0;
    }
}

int
main (int argc, char *argv[]) {
    char                *config_file = NULL;
    netsnmp_log_handler *log_hdl = NULL;
    int                 c, log_level = LOG_INFO;
    struct stat         logdir_s;
    pid_t               child_pid;
    FILE                *pid_fp;

    /* Load options */
    while ((--argc > 0) && ((*++argv)[0] == '-')) {
        while (c = *++argv[0]) {
            switch (c) {
            case 'D':
                log_level = LOG_DEBUG;
                break;
            default:
                printf("ldap-agent: illegal option %c\n", c);
                exit_usage();
            }
        }
    }

    if (argc != 1)
        exit_usage();

    /* load config file */
    if ((config_file = strdup(*argv)) == NULL) {
        printf("ldap-agent: Memory error loading config file\n");
        exit(1);
    }

    load_config(config_file);

    /* check if we're already running as another process */
    if ((pid_fp = fopen(pidfile, "r")) != NULL) {
        fscanf(pid_fp, "%d", &child_pid);
        fclose(pid_fp);
        if (kill(child_pid, SIGUSR1) == 0) {
            printf("ldap-agent: Already running as pid %d!\n", child_pid);
            exit(1);
        } else {
            /* old pidfile exists, but the process doesn't. Cleanup pidfile */
            remove(pidfile);
        }
    }

    /* start logging */
    netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID,
                           NETSNMP_DS_LIB_LOG_TIMESTAMP, 1);

    if ((log_hdl = netsnmp_register_loghandler(NETSNMP_LOGHANDLER_FILE,
                                               log_level)) != NULL) {
        if (agent_logdir != NULL) {
            /* Verify agent-logdir setting */
            if (stat(agent_logdir, &logdir_s) < 0) {
                printf("ldap-agent: Error reading logdir: %s\n", agent_logdir);
                exit(1);
            } else {
                /* Is it a directory? */
                if (S_ISDIR(logdir_s.st_mode)) {
                    /* Can we write to it? */
                    if (access(agent_logdir, W_OK) < 0) {
                        printf("ldap-agent: Unable to write to logdir: %s\n",
                                agent_logdir);
                        exit(1);
                    }
                } else {
                    printf("ldap-agent: agent-logdir setting must point to a directory.\n");
                    exit(1);
                }
            }

            /* agent-logdir setting looks ok */
            if ((log_hdl->token = malloc(strlen(agent_logdir) +
                                   strlen(LDAP_AGENT_LOGFILE) + 2)) != NULL) {
                strncpy((char *) log_hdl->token, agent_logdir, strlen(agent_logdir) + 1);
                /* add a trailing slash if needed */
                if (*(agent_logdir + strlen(agent_logdir)) != '/')
                    strcat((char *) log_hdl->token, "/");
                strcat((char *) log_hdl->token, LDAP_AGENT_LOGFILE);
            }
        } else {
            /* agent-logdir not set, so write locally */
            log_hdl->token = strdup(LDAP_AGENT_LOGFILE);
        } 

        netsnmp_enable_filelog(log_hdl, 1);
    } else {
        printf("Error starting logging.");
        exit(1);
    }

    snmp_log(LOG_INFO, "Starting ldap-agent...\n");

    /* setup agentx master */
    netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
                           NETSNMP_DS_AGENT_ROLE, 1);
    if (agentx_master)
        netsnmp_ds_set_string(NETSNMP_DS_APPLICATION_ID,
                              NETSNMP_DS_AGENT_X_SOCKET, agentx_master);

    /* run as a daemon */
    if (netsnmp_daemonize(0, 0)) {
        /* sleep to allow pidfile to be created by child */
        sleep(3);
        if((pid_fp = fopen(pidfile,"r")) == NULL) {
            printf("ldap-agent: Not started!  Check log file for details.\n");
            exit(1);
        } else {
            fscanf(pid_fp, "%d", &child_pid);
            fclose(pid_fp);
        }
        printf("ldap-agent: Started as pid %d\n", child_pid);
        exit(1);
    }

    /* initialize the agent */
    init_agent("ldap-agent");
    init_ldap_agent();  
    init_snmp("ldap-agent");

    /* listen for signals */
    keep_running = 1;
    signal(SIGUSR1, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGINT, stop_server);

    /* create pidfile in config file dir */
    child_pid = getpid();
    if ((pid_fp = fopen(pidfile, "w")) == NULL) {
        snmp_log(LOG_ERR, "Error creating pid file: %s\n", pidfile);
        exit(1);
    } else {
        if (fprintf(pid_fp, "%d", child_pid) < 0) {
            snmp_log(LOG_ERR, "Error writing pid file: %s\n", pidfile);
            exit(1);
        }
        fclose(pid_fp);
    }

    /* we're up and running! */
    snmp_log(LOG_INFO, "Started ldap-agent as pid %d\n", child_pid);

    /* loop here until asked to stop */
    while(keep_running) {
      agent_check_and_process(1);
    }

    /* say goodbye */
    snmp_shutdown("ldap-agent");
    snmp_log(LOG_INFO, "ldap-agent stopped.\n");

    /* remove pidfile */ 
    remove(pidfile);

    return 0;
}

/************************************************************************
 * load_config
 *
 * Loads subagent config file and reads directory server config files.
 */
void
load_config(char *conf_path)
{
    server_instance *serv_p = NULL;
    FILE *conf_file = NULL;
    FILE *dse_fp = NULL;
    char line[MAXLINE];
    char *p = NULL;
    char *p2 = NULL;

    /* Open config file */
    if ((conf_file = fopen(conf_path, "r")) == NULL) {
        printf("ldap-agent: Error opening config file: %s\n", conf_path);
        exit(1);
    } 

    /* set pidfile path */
    for (p = (conf_path + strlen(conf_path) - 1); p >= conf_path; p--) {
        if (*p == '/') {
            if ((pidfile = malloc((p - conf_path) +
                                   strlen(LDAP_AGENT_PIDFILE) + 2)) != NULL) {
                strncpy(pidfile, conf_path, (p - conf_path + 1));
                strcat(pidfile, LDAP_AGENT_PIDFILE);
                break;
            } else {
                printf("ldap-agent: malloc error processing config file\n");
                exit(1);
            }
        }
    }

    while (fgets(line, MAXLINE, conf_file) != NULL) {
        /* Ignore comment lines in config file */
        if (line[0] == '#')
            continue;

        if ((p = strstr(line, "agentx-master")) != NULL) {
            /* load agentx-master setting */
            p = p + 13;
            if ((p = strtok(p, " \t\n")) != NULL) {
                if ((agentx_master = (char *) malloc(strlen(p) + 1)) != NULL)
                    strcpy(agentx_master, p);
            }
        } else if ((p = strstr(line, "agent-logdir")) != NULL) {
            /* load agent-logdir setting */
            p = p + 12;
            if ((p = strtok(p, " \t\n")) != NULL) {
                if ((agent_logdir = (char *) malloc(strlen(p) + 1)) != NULL)
                    strcpy(agent_logdir, p);
            }
        } else if ((p = strstr(line, "server")) != NULL) {
            /* Allocate a server_instance */
            if ((serv_p = malloc(sizeof(server_instance))) == NULL) {
                printf("ldap-agent: malloc error processing config file\n");
                exit(1);
            }

            /* load server setting */
            p = p + 6;
            if ((p = strtok_r(p, " :\t\n", &p2)) != NULL) {
                /* first token is the instance root */
                if ((serv_p->stats_file = malloc(strlen(p) + 18)) != NULL)
                    snprintf(serv_p->stats_file, strlen(p) + 18,
                                     "%s/logs/slapd.stats", p);
                if ((serv_p->dse_ldif = malloc(strlen(p) + 17)) != NULL) {
                    snprintf(serv_p->dse_ldif, strlen(p) + 17, "%s/config/dse.ldif", p);
                }

                /* second token is the name */
                p = p2;
                if((p2 = strchr(p, ':')) != NULL) {
                    *p2 = '\0';
                    ++p2;
                    if ((serv_p->name = malloc(strlen(p) + 1)) != NULL)
                        snprintf(serv_p->name, strlen(p) + 1, "%s", p);
                } else {
                    printf("ldap-agent: Invalid config file\n");
                    exit(1);
                }
                
                /* third token is the description */
                p = p2;
                if((p2 = strchr(p, ':')) != NULL) {
                    *p2 = '\0';
                    ++p2;
                    if ((serv_p->description = malloc(strlen(p) + 1)) != NULL)
                        snprintf(serv_p->description, strlen(p) + 1, "%s", p);
                } else {
                    printf("ldap-agent: Invalid config file\n");
                    exit(1);
                }

                /* fourth token is the org */
                p = p2;
                if((p2 = strchr(p, ':')) != NULL) {
                    *p2 = '\0';
                    ++p2;
                    if ((serv_p->org = malloc(strlen(p) + 1)) != NULL)
                        snprintf(serv_p->org, strlen(p) + 1, "%s", p);
                } else {
                    printf("ldap-agent: Invalid config file\n");
                    exit(1);
                }

                /* fifth token is the location */
                p = p2;
                if((p2 = strchr(p, ':')) != NULL) {
                    *p2 = '\0';
                    ++p2;
                    if ((serv_p->location = malloc(strlen(p) + 1)) != NULL)
                        snprintf(serv_p->location, strlen(p) + 1, "%s", p);
                } else {
                    printf("ldap-agent: Invalid config file\n");
                    exit(1);
                }

                /* sixth token is the contact */
                p = p2;
                if((p2 = strchr(p, '\n')) != NULL) {
                    *p2 = '\0';
                    if ((serv_p->contact = malloc(strlen(p) + 1)) != NULL)
                        snprintf(serv_p->contact, strlen(p) + 1, "%s", p);
                } else {
                    printf("ldap-agent: Invalid config file\n");
                    exit(1);
                }
            }
 
            /* Open dse.ldif */
            if ((dse_fp = fopen(serv_p->dse_ldif, "r")) == NULL) {
                printf("ldap-agent: Error opening server config file: %s\n",
                        serv_p->dse_ldif);
                exit(1);
            }

            /* Get port value */
            while (fgets(line, MAXLINE, dse_fp) != NULL) {
                if ((p = strstr(line, "nsslapd-port: ")) != NULL) {
                    p = p + 14;
                    if ((p = strtok(p, ": \t\n")) != NULL)
                        serv_p->port = atol(p);
                }
            }

            /* Close dse.ldif */
            fclose(dse_fp);

            /* Insert server instance into linked list */
            serv_p->next = server_head;
            server_head = serv_p;
        }
    }

    /* Close config file */
    fclose(conf_file);

    /* check for at least one directory server instance */
    if (server_head == NULL) {
        printf("ldap-agent: No server instances defined in config file\n");
        exit(1);
    }
}

/************************************************************************
 * exit_usage
 *
 * Prints usage message and exits program.
 */
void
exit_usage()
{
    printf("Usage: ldap-agent [-D] configfile\n");
    printf("       -D    Enable debug logging\n");
    exit(1);
}
