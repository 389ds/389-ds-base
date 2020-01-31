var DS_HOME = "/etc/dirsrv/";
var server_id = "None";
var server_inst = "";
var dn_regex = new RegExp( "^([A-Za-z]+=.*)" );

/*
 * We can't load the config until all the html pages are load, so we'll use vars
 * to track the loading, and once all the pages are loaded, then we can load the config
 */
var server_page_loaded = 1;
var security_page_loaded = 1;
var db_page_loaded = 1;
var repl_page_loaded = 1;
var plugin_page_loaded = 1;
var schema_page_loaded = 1;
var monitor_page_loaded = 1;
var config_loaded = 0;

// objects to store original values (used for comparing what changed when saving
var config_values = {};
var localpwp_values = {};
var repl_config_values = {};
var repl_cl_values = {};

//TODO - need "config_values" for all the other pages: backend, replication, suffix, etc.

var DSCONF = "dsconf";
var DSCTL = "dsctl";
var DSCREATE = "dscreate";
var ENV = "";


/*
 * Console logging function for CLI commands
 *
 * @param {text}   js_func    The javascript/jquery function that is making this call.
 * @param {text}   desc       The description of the CLI command.
 * @param {array}  cmd_array  An array of all the CLI arguments.
 */
function log_cmd(js_func, desc, cmd_array) {
  if (window.console) {
    var pw_args = ['--passwd', '--bind-pw', '--bind-passwd', '--nsslapd-rootpw'];
    var cmd_list = [];
    var converted_pw = false;

    for (var idx in cmd_array) {
      var cmd = cmd_array[idx].toString();
      converted_pw = false;
      for (var arg_idx in pw_args) {
        if ( cmd.startsWith(pw_args[arg_idx]) ) {
          // We are setting a password, if it has a value we need to hide it
          var arg_len = cmd.indexOf('=');
          var arg = cmd.substring(0, arg_len);
          if (cmd.length != arg_len + 1) {
            // We are setting a password value...
            cmd_list.push(arg + "=**********");
            converted_pw = true;
          }
          break;
        }
      }
      if (!converted_pw) {
        cmd_list.push(cmd);
      }
    }
    window.console.log("CMD: " + js_func + ": " + desc + " ==> " + cmd_list.join(' '));
  }
}

// TODO validation functions

function valid_dn (dn){
  // Validate value is a valid DN (sanity validation)
  var result = dn_regex.test(dn);
  return result;
}

function valid_num (val){
  // Validate value is a number
  let result = !isNaN(val);
  return result;
}

function valid_port (val){
  // Validate value is a number and between 1 and 65535
  let result = !isNaN(val);
  if (result) {
      if (val < 1 || val > 65535) {
          result = false;
      }
  }
  return result;
}

function tableize (val) {
  // Truncate a long value to fit inside table
  if (val.length > 25){
    val = val.substring(0,25) + "...";
  }
  return val;
}

/*
 * Set the ports numbers on the instance creation form.  If the default ports
 * are taken just unset the values.
 */
function set_ports() {
  var cmd = ['ss', '-ntpl'];
  cockpit.spawn(cmd, { superuser: true, "err": "message"}).done(function(data) {
    var lines = data.split('\n');
    $("#create-inst-port").val("389");
    $("#create-inst-secureport").val("636");
    for (var i = 0; i < lines.length; i++){
      if (lines[i].indexOf("LISTEN") != -1 && lines[i].indexOf(":389 ") != -1) {
        $("#create-inst-port").val("");
      }
      if (lines[i].indexOf("LISTEN") != -1 && lines[i].indexOf(":636 ") != -1) {
        $("#create-inst-secureport").val("");
      }
    }
  });
}

function sort_list (sel) {
  var opts_list = sel.find('option');
  opts_list.sort(function(a, b) { return $(a).text() > $(b).text() ? 1 : -1; });
  sel.html('').append(opts_list);
}


function get_date_string (timestamp) {
  // Convert DS timestamp to a friendly string: 20180921142257Z -> 10/21/2018, 2:22:57 PM
  let year = timestamp.substr(0,4);
  let month = timestamp.substr(4,2);
  let day = timestamp.substr(6,2);
  let hour = timestamp.substr(8,2);
  let minute = timestamp.substr(10,2);
  let sec = timestamp.substr(12,2);
  let date = new Date(parseInt(year), parseInt(month), parseInt(day),
                      parseInt(hour), parseInt(minute), parseInt(sec));

  return date.toLocaleString();
}

function get_date_diff(start, end) {
    // Get the start up date
    let year = start.substr(0,4);
    let month = start.substr(4,2);
    let day = start.substr(6,2);
    let hour = start.substr(8,2);
    let minute = start.substr(10,2);
    let sec = start.substr(12,2);
    let startDate = new Date(parseInt(year), parseInt(month), parseInt(day),
                             parseInt(hour), parseInt(minute), parseInt(sec));

    // Get the servers current date
    year = end.substr(0,4);
    month = end.substr(4,2);
    day = end.substr(6,2);
    hour = end.substr(8,2);
    minute = end.substr(10,2);
    sec = end.substr(12,2);
    let currDate = new Date(parseInt(year), parseInt(month), parseInt(day),
                            parseInt(hour), parseInt(minute), parseInt(sec));

    // Generate pretty elapsed time string
    let seconds = Math.floor((startDate - (currDate))/1000);
    let minutes = Math.floor(seconds/60);
    let hours = Math.floor(minutes/60);
    let days = Math.floor(hours/24);
    hours = hours-(days*24);
    minutes = minutes-(days*24*60)-(hours*60);
    seconds = seconds-(days*24*60*60)-(hours*60*60)-(minutes*60);

    return("${days} days, ${hours} hours, ${minutes} minutes, and ${seconds} seconds");
}

function set_no_insts () {
    $("#select-server").empty();
    $("#select-server").append('<option value="No instances">No instances</option>');
    $("#select-server select").val('No instances');

    server_id = "";
    server_inst = "";

    $("#server-list-menu").attr('disabled', true);
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#no-instances").show();
}

function check_for_389 () {
  var cmd = ["rpm", "-q", "389-ds-base"];

  cockpit.spawn(cmd, { superuser: true }).fail(function(data) {
    $("#server-list-menu").attr('disabled', true);
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#no-package").show();
  });
}

function check_inst_alive (connect_err) {
  // Check if this instance is started, if not hide configuration pages
  if (connect_err === undefined) {
    connect_err = 0;
  }
  cmd = [DSCTL, '-j', server_inst, 'status'];
  cockpit.spawn(cmd, { superuser: true}).
  done(function(status_data) {
    var status_json = JSON.parse(status_data);
    if (status_json.running == true) {
      if (connect_err) {
        $("#ds-navigation").hide();
        $(".all-pages").hide();
        $("#no-connect").show();
      } else {
        // if nav page was hidden reset everything
        if ($("#ds-navigation").is(":hidden") ){
          $(".all-pages").hide();
          $("#ds-navigation").show();
          $("#server-content").show();
        }
        $("#not-running").hide();
        $("#no-connect").hide();
      }
    } else {
      $("#loading-page").hide();
      $("#ds-navigation").hide();
      $(".all-pages").hide();
      $("#not-running").show();
    }
  }).fail(function(data) {
    $("#loading-page").hide();
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#not-running").show();
  });
}

function get_insts() {
  // Load initial forms
  $("#server-list-menu").attr('disabled', false);
  $("#ds-navigation").show();
  $(".all-pages").hide();
  $("#no-instances").hide();

  var insts = [];
  var cmd = ["/bin/sh", "-c", "/usr/bin/ls -d " + DS_HOME + "slapd-*"];
  cockpit.spawn(cmd, { superuser: true }).done(function(data) {
    // Parse the output, and skip removed instances and empty lines
    var lines = data.split('\n');
    var i = 0;
    for (i = 0; i < lines.length; i++) {
      if (lines[i].endsWith(".removed") == false && lines[i] != "") {
        var serverid = lines[i].replace(DS_HOME, "");
        insts.push(serverid);
      }
    }

    if (server_id != "None") {
      $("#ds-banner").html("Managing Instance <select class=\"btn btn-default dropdown\" id=\"select-server\"></select>");
    }

    // Populate the server instance drop down
    $("#select-server").empty();
    for (i = 0; i < insts.length; i++) {
      $("#select-server").append('<option value="' + insts[i] + '">' + insts[i] +'</option>');
      $("#select-server select").val(insts[i]);
    }

    // Handle changing instance here
    document.getElementById("select-server").addEventListener("change", function() {
      server_id = $(this).val();
      server_inst = server_id.replace("slapd-", "");
      load_config();
    });

    if (insts[0] === undefined) {
      set_no_insts();
      $("#loading-page").hide();
      $("#everything").show();
    } else {
      // We have at least one instance, make sure we "open" the UI
      server_id = insts[0];
      server_inst = insts[0].replace("slapd-", "");
      check_inst_alive();
      // We have to dispatch an event for the React components rerender
      // It should also trigger the listener defined before
      server_select_elem = document.getElementById('select-server');
      server_select_elem.dispatchEvent(new Event('change'));
    }
  }).fail(function(error){
    set_no_insts();
    $("#loading-page").hide();
    $("#everything").show();
  });
}

function report_err( input, msg) {
  $(".ds-modal-error").html('Error: ' + msg);
  input.css("border-color", "red");
  $(".ds-modal-error").show();
}


function popup_err(title, msg) {
  // Display errors from the cli (we have to use pre tags)
  bootpopup({
    title: title,
    content: [
      '<pre>' + msg + '</pre>'
    ]
  });
  check_inst_alive(0);
}

function popup_msg(title, msg) {
  bootpopup({
    title: title,
    content: [
      '<p>' + msg + '</p>'
    ]
  });
}

function popup_confirm(msg, title, callback) {
  if(typeof callback !== "function") {
    callback = function() {};
  }
  var answer = false;
  return bootpopup({
    title: title,
    content: [
      msg
    ],
    showclose: false,
    buttons: ["no", "yes"],
    yes: function() { answer = true; },
    dismiss: function() { callback(answer); },
  });
}

function popup_success(msg) {
  $('#success-msg').html(msg);
  $('#success-form').modal('show');
  setTimeout(function() {$('#success-form').modal('hide');}, 2000);
}

// This is called when any Save button is clicked on the main page.  We call
// all the save functions for all the pages here.  This is not used for modal forms
function save_all () {
    save_config();  // Server Config Page
}

var progress = 10;

function update_progress () {
    progress += 10;
    if (progress > 100) {
        progress = 100;
    }
    $("#ds-progress-label").text(progress + "%");
    $("#ds-progress-bar").attr("aria-valuenow", progress);
    $("#ds-progress-bar").css("width", progress + "%");
}

var loading_cfg = 0;

function load_config (refresh){
  // If we are currently loading config don't do it twice
  if (loading_cfg == 1){
    return;
  }
  loading_cfg = 1;
  progress = 10;
  update_progress();

  // Show the spinner, and reset the pages
  $("#loading-msg").html("Loading Directory Server configuration for <i><b>" + server_id + "</b></i>...");
  $("#everything").hide();
  $(".all-pages").hide();
  $("#loading-page").show();
  config_loaded = 0;

  /*
   * Start with the dropdowns, if this fails we stop here, otherwise we assume
   * we are up and running and we can load the other config/
   */
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','backend', 'suffix', 'list', '--suffix'];
  log_cmd('load_config', 'get backend list', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    update_progress();
    config_loaded = 1;

    // Initialize the tabs
    $(".ds-tab-list").css( 'color', '#777');
    $("#server-tab").css( 'color', '#228bc0');

    // Set an interval event to wait for all the pages to load, then show the content
    var loading_config = setInterval(function() {
      if (config_loaded == 1) {
        $("#loading-page").hide();
        $("#everything").show();
        $("#server-content").show();
        clearInterval(loading_config);
        loading_cfg = 0;

        if (refresh) {
            // Reload reactJS pages by clicking dummy element
            let reload_el = document.getElementById('reload-page');
            reload_el.click();
        }

        console.log("Completed configuration initialization.");
      }
    }, 300);

  }).fail(function(data) {
    popup_err("Failed To Contact Server",data.message);
    $("#everything").show();
    check_inst_alive(1);
    loading_cfg = 0;
  });
}



// Create Instance
$("#create-inst-save").on("click", function() {
    $(".ds-modal-error").hide();
    $(".ds-inst-input").css("border-color", "initial");

    /*
     * Validate settings and update the INF settings
     */
    let setup_inf = create_inf_template;

    // Server ID
    let new_server_id = $("#create-inst-serverid").val();
    if (new_server_id == ""){
        report_err($("#create-inst-serverid"), 'You must provide an Instance name');
        $("#create-inst-serverid").css("border-color", "red");
        return;
    } else {
        new_server_id = new_server_id.replace(/^slapd-/i, "");  // strip "slapd-"
        if (new_server_id.length > 128) {
            report_err($("#create-inst-serverid"), 'Instance name is too long, it must not exceed 128 characters');
            $("#create-inst-serverid").css("border-color", "red");
            return;
        }
        if (new_server_id.match(/^[#%:A-Za-z0-9_\-]+$/g)) {
            setup_inf = setup_inf.replace('INST_NAME', new_server_id);
        } else {
            report_err($("#create-inst-serverid"), 'Instance name can only contain letters, numbers, and:  # % : - _');
            $("#create-inst-serverid").css("border-color", "red");
            return;
        }
    }

    // Port
    let server_port = $("#create-inst-port").val();
    if (server_port == ""){
        report_err($("#create-inst-port"), 'You must provide a port number');
        $("#create-inst-port").css("border-color", "red");
        return;
    } else if (!valid_port(server_port)) {
        report_err($("#create-inst-port"), 'Port must be a number between 1 and 65534!');
        $("#create-inst-port").css("border-color", "red");
        return;
    } else {
        setup_inf = setup_inf.replace('PORT', server_port);
    }

    // Secure Port
    let secure_port = $("#create-inst-secureport").val();
    if (secure_port == ""){
        report_err($("#create-inst-secureport"), 'You must provide a secure port number');
        $("#create-inst-secureport").css("border-color", "red");
        return;
    } else if (!valid_port(secure_port)) {
        report_err($("#create-inst-secureport"), 'Secure port must be a number!');
        $("#create-inst-secureport").css("border-color", "red");
        return;
    } else {
        setup_inf = setup_inf.replace('SECURE_PORT', secure_port);
    }

    // Root DN
    let server_rootdn = $("#create-inst-rootdn").val();
    if (server_rootdn == ""){
        report_err($("#create-inst-rootdn"), 'You must provide a Directory Manager DN');
        $("#create-inst-rootdn").css("border-color", "red");
        return;
    } else {
        setup_inf = setup_inf.replace('ROOTDN', server_rootdn);
    }

    // Setup Self-Signed Certs
    if ( $("#create-inst-tls").is(":checked") ){
        setup_inf = setup_inf.replace('SELF_SIGN', 'True');
    } else {
        setup_inf = setup_inf.replace('SELF_SIGN', 'False');
    }

    // Root DN password
    let root_pw = $("#rootdn-pw").val();
    let root_pw_confirm = $("#rootdn-pw-confirm").val();
    if (root_pw != root_pw_confirm) {
        report_err($("#rootdn-pw"), 'Directory Manager passwords do not match!');
        $("#rootdn-pw-confirm").css("border-color", "red");
        return;
    } else if (root_pw == ""){
        report_err($("#rootdn-pw"), 'Directory Manager password can not be empty!');
        $("#rootdn-pw-confirm").css("border-color", "red");
        return;
    } else if (root_pw.length < 8) {
        report_err($("#rootdn-pw"), 'Directory Manager password must have at least 8 characters');
        $("#rootdn-pw-confirm").css("border-color", "red");
        return;
    } else {
        setup_inf = setup_inf.replace('ROOTPW', root_pw);
    }

    // Backend/Suffix
    let backend_name = $("#backend-name").val();
    let backend_suffix = $("#backend-suffix").val();
    if ( (backend_name != "" && backend_suffix == "") || (backend_name == "" && backend_suffix != "") ) {
        if (backend_name == ""){
            report_err($("#backend-name"), 'If you specify a backend suffix, you must also specify a backend name');
            $("#backend-name").css("border-color", "red");
            return;
        } else {
            report_err($("#backend-suffix"), 'If you specify a backend name, you must also specify a backend suffix');
            $("#backend-suffix").css("border-color", "red");
            return;
        }
    }
    if (backend_name != ""){
        // We definitely have a backend name and suffix, next validate the suffix is a DN
        if (valid_dn(backend_suffix)) {
            // It's valid, add it
            setup_inf += "\n[backend-" + backend_name + "]\nsuffix = " + backend_suffix + "\n";
        } else {
            // Not a valid DN
            report_err($("#backend-suffix"), 'Invalid DN for Backend Suffix');
            return;
        }
        if ( $("#create-sample-entries").is(":checked") ) {
            setup_inf += '\nsample_entries = yes\n';
        } else if ( $("#create-suffix-entry").is(":checked") ) {
            setup_inf += '\ncreate_suffix_entry = yes\n';
        }
    }

    /*
     * Here are steps we take to create the instance
     *
     * [1] Get FQDN Name for nsslapd-localhost setting in setup file
     * [2] Create a file for the inf setup parameters
     * [3] Set strict permissions on that file
     * [4] Populate the new setup file with settings (including cleartext password)
     * [5] Create the instance
     * [6] Remove setup file
     */
    cockpit.spawn(["hostname", "--fqdn"], { superuser: true, "err": "message" }).fail(function(ex, data) {
        // Failed to get FQDN
        popup_err("Failed to get hostname!", data);
    }).done(function (data) {
        /*
         * We have FQDN, so set the hostname in inf file, and create the setup file
         */
        setup_inf = setup_inf.replace('FQDN', data);
        let setup_file = "/tmp/389-setup-" + (new Date).getTime() + ".inf";
        let rm_cmd = ['rm', setup_file];
        let create_file_cmd = ['touch', setup_file];
        cockpit.spawn(create_file_cmd, { superuser: true, "err": "message" }).fail(function(ex, data) {
            // Failed to create setup file
            popup_err("Failed to create installation file!", data);
        }).done(function (){
            /*
             * We have our new setup file, now set permissions on that setup file before we add sensitive data
             */
            let chmod_cmd = ['chmod', '600', setup_file];
            cockpit.spawn(chmod_cmd, { superuser: true, "err": "message" }).fail(function(ex, data) {
                // Failed to set permissions on setup file
                cockpit.spawn(rm_cmd, { superuser: true });  // Remove Inf file with clear text password
                $("#create-inst-spinner").hide();
                popup_err("Failed to set permission on setup file " + setup_file + ": ", data);
            }).done(function () {
                /*
                 * Success we have our setup file and it has the correct permissions.
                 * Now populate the setup file...
                 */
                let cmd = ["/bin/sh", "-c", '/usr/bin/echo -e "' + setup_inf + '" >> ' + setup_file];
                cockpit.spawn(cmd, { superuser: true, "err": "message" }).fail(function(ex, data) {
                    // Failed to populate setup file
                    popup_err("Failed to populate installation file!", data);
                }).done(function (){
                    /*
                     * Next, create the instance...
                     */
                     let cmd = [DSCREATE, 'from-file', setup_file];
                     cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV] }).fail(function(ex, data) {
                         // Failed to create the new instance!
                         cockpit.spawn(rm_cmd, { superuser: true });  // Remove Inf file with clear text password
                         $("#create-inst-spinner").hide();
                         popup_err("Failed to create instance!", data);
                     }).done(function (){
                         // Success!!!  Now cleanup everything up...
                         cockpit.spawn(rm_cmd, { superuser: true });  // Remove Inf file with clear text password
                         $("#create-inst-spinner").hide();
                         $("#server-list-menu").attr('disabled', false);
                         $("#no-instances").hide();
                         get_insts();  // Refresh server list
                         popup_success("Successfully created instance:  <b>slapd-" + new_server_id + "</b>");
                         $("#create-inst-form").modal('toggle');
                     });
                 });
                 $("#create-inst-spinner").show();
             });
        });
    }).fail(function(data) {
        console.log("failed: " + data.message);
    });
});

var create_full_template =
  "[general]\n" +
  "config_version = 2\n" +
  "defaults = 999999999\n" +
  "full_machine_name = FQDN\n" +
  "selinux = True\n" +
  "strict_host_checking = True\n" +
  "systemd = True\n" +
  "[slapd]\n" +
  "backup_dir = /var/lib/dirsrv/slapd-{instance_name}/bak\n" +
  "bin_dir = /usr/bin\n" +
  "cert_dir = /etc/dirsrv/slapd-{instance_name}\n" +
  "config_dir = /etc/dirsrv/slapd-{instance_name}\n" +
  "data_dir = /usr/share\n" +
  "db_dir = /var/lib/dirsrv/slapd-{instance_name}/db\n" +
  "user = dirsrv\n" +
  "group = dirsrv\n" +
  "initconfig_dir = /etc/sysconfig\n" +
  "inst_dir = /usr/lib64/dirsrv/slapd-{instance_name}\n" +
  "instance_name = localhost\n" +
  "ldif_dir = /var/lib/dirsrv/slapd-{instance_name}/ldif\n" +
  "lib_dir = /usr/lib64\n" +
  "local_state_dir = /var\n" +
  "lock_dir = /var/lock/dirsrv/slapd-{instance_name}\n" +
  "log_dir = /var/log/dirsrv/slapd-{instance_name}\n" +
  "port = PORT\n" +
  "prefix = /usr\n" +
  "root_dn = ROOTDN\n" +
  "root_password = ROOTPW\n" +
  "run_dir = /var/run/dirsrv\n" +
  "sbin_dir = /usr/sbin\n" +
  "schema_dir = /etc/dirsrv/slapd-{instance_name}/schema\n" +
  "secure_port = SECURE_PORT\n" +
  "self_sign_cert = True\n" +
  "sysconf_dir = /etc\n" +
  "tmp_dir = /tmp\n";

var create_inf_template =
  "[general]\n" +
  "config_version = 2\n" +
  "full_machine_name = FQDN\n\n" +
  "[slapd]\n" +
  "user = dirsrv\n" +
  "group = dirsrv\n" +
  "instance_name = INST_NAME\n" +
  "port = PORT\n" +
  "root_dn = ROOTDN\n" +
  "root_password = ROOTPW\n" +
  "secure_port = SECURE_PORT\n" +
  "self_sign_cert = SELF_SIGN\n";


function clear_inst_form() {
  $(".ds-modal-error").hide();
  $("#create-inst-serverid").val("");
  $("#create-inst-port").val("389");
  $("#create-inst-secureport").val("636");
  $("#create-inst-rootdn").val("cn=Directory Manager");
  $("#rootdn-pw").val("");
  $("#rootdn-pw-confirm").val("");
  $("#backend-suffix").val("dc=example,dc=com");
  $("#backend-name").val("userRoot");
  $("#create-sample-entries").prop('checked', false);
  $("#create-inst-tls").prop('checked', true);
  $(".ds-inst-input").css("border-color", "initial");
}

function do_backup(server_inst, backup_name) {
  var cmd = [DSCTL, '-j', server_inst, 'status'];
  $("#backup-spinner").show();
  cockpit.spawn(cmd, { superuser: true}).
  done(function(status_data) {
    var status_json = JSON.parse(status_data);
    if (status_json.running == true) {
      var cmd = [DSCONF, "-j", server_inst, 'backup', 'create',  backup_name];
      log_cmd('#ds-backup-btn (click)', 'Backup server instance', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#backup-spinner").hide();
        popup_success("Backup has been created");
        $("#backup-form").modal('toggle');
      }).
      fail(function(data) {
        $("#backup-spinner").hide();
        popup_err("Failed to backup the server", data.message);
      })
    } else {
      var cmd = [DSCTL, server_inst, 'db2bak', backup_name];
      log_cmd('#ds-backup-btn (click)', 'Backup server instance (offline)', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#backup-spinner").hide();
        popup_success("Backup has been created");
        $("#backup-form").modal('toggle');
      }).
      fail(function(data) {
        $("#backup-spinner").hide();
        popup_err("Failed to backup the server", data.message);
      });
    }
  }).
  fail(function() {
    popup_err("Failed to check the server status", data.message);
  });
}

$(window.document).ready(function() {
  if(navigator.userAgent.toLowerCase().indexOf('firefoxf') > -1) {
    $("select@@@").focus( function() {
      this.style.setProperty( 'outline', 'none', 'important' );
      this.style.setProperty( 'color', 'rgba(0,0,0,0)', 'important' );
      this.style.setProperty( 'text-shadow', '0 0 0 #000', 'important' );
    });
  }

  // Set an interval event to wait for all the pages to load, then load the config
  var init_config = setInterval(function() {
        /*
         *  Stop, Start, and Restart server
         */

        let banner = document.getElementById("start-server-btn");
        if (banner == null) {
          // Not ready yet, return and try again
          return;
        }

        get_insts();

        /* Restore.  load restore table with current backups */
        document.getElementById("restore-server-btn").addEventListener("click", function() {
          var cmd = [DSCTL, '-j', server_inst, 'backups'];
          log_cmd('#restore-server-btn (click)', 'Restore server instance', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            let backup_btn = "<button class=\"btn btn-default restore-btn\" type=\"button\">Restore</button>";
            let del_btn =  "<button title=\"Delete backup directory\" class=\"btn btn-default ds-del-backup-btn\" type=\"button\"><span class='glyphicon glyphicon-trash'></span></button>";
            let obj = JSON.parse(data);
            backup_table.clear().draw( false );
            for (var i = 0; i < obj.items.length; i++) {
              let backup_name = obj.items[i][0];
              let backup_date = obj.items[i][1];
              let backup_size = obj.items[i][2];
              backup_table.row.add([backup_name, backup_date, backup_size, backup_btn, del_btn]).draw( false );
            }
          }).fail(function(data) {
            popup_err("Failed to get list of backups", data.message);
          });
        });

        document.getElementById("backup-server-btn").addEventListener("click", function() {
          $("#backup-name").val("");
        });

        document.getElementById("start-server-btn").addEventListener("click", function() {
          $("#ds-start-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Starting instance <b>" + server_id + "</b>...");
          $("#start-instance-form").modal('toggle');
          var cmd = [DSCTL, server_inst, 'start'];
          log_cmd('#start-server-btn (click)', 'Start server instance', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            $("#start-instance-form").modal('toggle');
            load_config(true);
            popup_success("Started instance \"" + server_id + "\"");
          }).fail(function(data) {
            $("#start-instance-form").modal('toggle');
            popup_err("Failed to start instance \"" + server_id,  data.message);
          });
        });

        document.getElementById("stop-server-btn").addEventListener("click", function() {
          $("#ds-stop-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Stopping instance <b>" + server_id + "</b>...");
          $("#stop-instance-form").modal('toggle');
          var cmd = [DSCTL, server_inst, 'stop'];
          log_cmd('#stop-server-btn (click)', 'Stop server instance', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            $("#stop-instance-form").modal('toggle');
            popup_success("Stopped instance \"" + server_id + "\"");
            check_inst_alive();
          }).fail(function(data) {
            $("#stop-instance-form").modal('toggle');
            popup_err("Error", "Failed to stop instance \"" + server_id+ "\"", data.message);
            check_inst_alive();
          });
        });


        document.getElementById("restart-server-btn").addEventListener("click", function() {
          $("#ds-restart-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Restarting instance <b>" + server_id + "</b>...");
          $("#restart-instance-form").modal('toggle');
          var cmd = [DSCTL, server_inst, 'restart'];
          log_cmd('#restart-server-btn (click)', 'Restart server instance', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            $("#restart-instance-form").modal('toggle');
            load_config(true);
            popup_success("Restarted instance \"" + server_id + "\"");
          }).fail(function(data) {
            $("#restart-instance-form").modal('toggle');
            popup_err("Failed to restart instance \"" + server_id + "\"", data.message);
          });
        });

        document.getElementById("remove-server-btn").addEventListener("click", function() {
          popup_confirm("Are you sure you want to this remove instance: <b>" + server_id + "</b>", "Confirmation", function (yes) {
            if (yes) {
              var cmd = [DSCTL, server_inst, "remove", "--do-it"];
              $("#ds-remove-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Removing instance <b>" + server_id + "</b>...");
              $("#remove-instance-form").modal('toggle');
              log_cmd('#remove-server-btn (click)', 'Remove instance', cmd);
              cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                $("#remove-instance-form").modal('toggle');
                popup_success("Instance has been deleted");
                get_insts();
              }).fail(function(data) {
                $("#remove-instance-form").modal('toggle');
                popup_err("Failed to remove instance", data.message);
              });
            }
          });
        });
        clearInterval(init_config);
  }, 250);

  $("#main-banner").load("banner.html");
  check_for_389();

  $("#server-tab").css( 'color', '#228bc0');  // Set first tab as highlighted

  // Events
  $(".ds-nav-choice").on('click', function (){
    // This highlights each nav tab when clicked
    $(".ds-tab-list").css( 'color', '#777');
    var tab = $(this).attr("parent-id");
    $("#" + tab).css( 'color', '#228bc0');
  });

  $("#server-tasks-btn").on("click", function() {
    $(".all-pages").hide();
    $("#server-tasks").show();
  });
  $("#server-tab").on("click", function() {
    $(".all-pages").hide();
    $("#server-content").show();
  });
  $("#plugin-tab").on("click", function() {
    $(".all-pages").hide();
    $("#plugin-content").show();
  });
  $("#database-tab").on("click", function() {
    $(".all-pages").hide();
    $("#database-content").show();
  });
  $("#monitor-tab").on("click", function() {
    $(".all-pages").hide();
    $("#monitor-content").show();
  });
  $("#schema-tab").on("click", function() {
    $(".all-pages").hide();
    $("#schema-content").show();
  });
  $("#replication-tab").on("click", function() {
    $(".all-pages").hide();
    $("#replication-content").show();
  });

  // Create instance form
  $("#create-server-btn").on("click", function() {
    clear_inst_form();
    set_ports();
  });
  $("#no-inst-create-btn").on("click", function () {
    clear_inst_form();
  });

  // backup/restore table
  var backup_table = $('#backup-table').DataTable( {
    "paging": true,
    "bAutoWidth": false,
    "dom": '<"pull-left"f><"pull-right"l>tip',
    "lengthMenu": [ 10, 25, 50, 100],
    "language": {
      "emptyTable": "No backups available for restore",
      "search": "Search Backups"
    },
    "columnDefs": [ {
      "targets": [3, 4],
      "orderable": false
    } ],
    "columns": [
      { "width": "120px" },
      { "width": "80px" },
      { "width": "30px" },
      { "width": "40px" },
      { "width": "30px" }
    ],
  });

  $(".all-pages").hide();
  $("#server-content").show();

  // To remove text border on firefox on dropdowns)
  if(navigator.userAgent.toLowerCase().indexOf('firefox') > -1) {
    $("select").focus( function() {
      this.style.setProperty( 'outline', 'none', 'important' );
      this.style.setProperty( 'color', 'rgba(0,0,0,0)', 'important' );
      this.style.setProperty( 'text-shadow', '0 0 0 #000', 'important' );
    });
  }

  $(".ds-tab-standalone").on('click', function (){
    $(".ds-tab-list").css( 'color', '#777');
    $(this).css( 'color', '#228bc0');
  });

  /* Backup server */
  $("#ds-backup-btn").on('click', function () {
    var backup_name = $("#backup-name").val();
    if (backup_name == ""){
      popup_msg("Error", "Backup must have a name");
      return;
    }
    if (backup_name.indexOf(' ') >= 0) {
      popup_msg("Error", "Backup name can not contain any spaces");
      return;
    }
    if (backup_name.indexOf('/') >= 0) {
      popup_msg("Error", "Backup name can not contain a forward slash. " +
                         "Backups are written to the server's backup directory (nsslapd-bakdir)");
      return;
    }

    // First check if backup name is already used
    var check_cmd = [DSCTL, '-j', server_inst, 'backups'];
    log_cmd('#ds-backup-btn (click)', 'Restore server instance', check_cmd);
    cockpit.spawn(check_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      var obj = JSON.parse(data);
      var found_backup = false;
      for (var i = 0; i < obj.items.length; i++) {
        if (obj.items[i][0] == backup_name) {
          found_backup = true;
          break;
        }
      }
      if (found_backup) {
        popup_confirm("A backup already exists with this name, replace it?", "Confirmation", function (yes) {
          if (yes) {
            do_backup(server_inst, backup_name);
          } else {
            return;
          }
        });
      } else {
        do_backup(server_inst, backup_name);
      }
    });
  });

  /* Restore server */
  $(document).on('click', '.restore-btn', function(e) {
    e.preventDefault();
    var data = backup_table.row( $(this).parents('tr') ).data();
    var restore_name = data[0];
    popup_confirm("Are you sure you want to restore this backup:  <b>" + restore_name + "<b>", "Confirmation", function (yes) {
      if (yes) {
        var cmd = [DSCTL, '-j', server_inst, 'status'];
        $("#restore-spinner").show();
        cockpit.spawn(cmd, { superuser: true}).
        done(function(status_data) {
          var status_json = JSON.parse(status_data);
          if (status_json.running == true) {
            var cmd = [DSCONF, server_inst, 'backup', 'restore',  restore_name];
            log_cmd('.restore-btn (click)', 'Restore server instance(online)', cmd);
            cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
            done(function(data) {
              $("#restore-spinner").hide();
              popup_success("The backup has been restored");
              $("#restore-form").modal('toggle');
            }).
            fail(function(data) {
              $("#restore-spinner").hide();
              popup_err("Failed to restore from the backup", data.message);
            });
          } else {
            var cmd = [DSCTL, server_inst, 'bak2db', restore_name];
            log_cmd('.restore-btn (click)', 'Restore server instance(offline)', cmd);
            cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
            done(function(data) {
              $("#restore-spinner").hide();
              popup_success("The backup has been restored");
              $("#restore-form").modal('toggle');
            }).
            fail(function(data) {
              $("#restore-spinner").hide();
              popup_err("Failed to restore from the backup", data.message);
            });
          }
        }).
        fail(function() {
          popup_err("Failed to check the server status", data.message);
        });
      }
    });
  });

  /* Delete backup directory */
  $(document).on('click', '.ds-del-backup-btn', function(e) {
    e.preventDefault();
    var data = backup_table.row( $(this).parents('tr') ).data();
    var restore_name = data[0];
    var backup_row = $(this);
    popup_confirm("Are you sure you want to delete this backup: <b>" + restore_name + "</b>", "Confirmation", function (yes) {
      if (yes) {
        var cmd = [DSCTL, server_inst, 'backups', '--delete', restore_name];
        $("#restore-spinner").show();
        log_cmd('.ds-del-backup-btn (click)', 'Delete backup', cmd);
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
          $("#restore-spinner").hide();
          backup_table.row( backup_row.parents('tr') ).remove().draw( false );
          popup_success("The backup has been deleted");
        }).fail(function(data) {
          $("#restore-spinner").hide();
          popup_err("Failed to delete the backup", data.message);
        });
      }
    });
  });

  /* reload schema */
  $("#schema-reload-btn").on("click", function () {
    var schema_dir = $("#reload-dir").val();
    if (schema_dir != ""){
      var cmd = [DSCONF, server_inst, 'schema', 'reload', '--schemadir', schema_dir, '--wait'];
    } else {
      var cmd = [DSCONF, server_inst, 'schema', 'reload', '--wait'];
    }
    $("#reload-spinner").show();
    log_cmd('#schema-reload-btn (click)', 'Reload schema files', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      popup_success("Successfully reloaded schema");  // TODO use timed interval success msg (waiting for another PR top be merged before we can add it)
      $("#schema-reload-form").modal('toggle');
      $("#reload-spinner").hide();
    }).fail(function(data) {
      popup_err("Failed to reload schema files", data.message);
      $("#reload-spinner").hide();
    });
  });


});
