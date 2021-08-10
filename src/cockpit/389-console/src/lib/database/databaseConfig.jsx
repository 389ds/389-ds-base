import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    ExpandableSection,
    Grid,
    GridItem,
    TextInput,
    Spinner,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
    noop
} from "@patternfly/react-core";

import PropTypes from "prop-types";

export class GlobalDatabaseConfig extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            isExpanded: false,
            saving: false,
            saveBtnDisabled: true,
            db_cache_auto: this.props.data.db_cache_auto,
            import_cache_auto: this.props.data.import_cache_auto,
            looklimit: this.props.data.looklimit,
            idscanlimit: this.props.data.idscanlimit,
            pagelooklimit: this.props.data.pagelooklimit,
            pagescanlimit: this.props.data.pagescanlimit,
            rangelooklimit: this.props.data.rangelooklimit,
            autosize: this.props.data.autosize,
            autosizesplit: this.props.data.autosizesplit,
            dbcachesize: this.props.data.dbcachesize,
            txnlogdir: this.props.data.txnlogdir,
            dbhomedir: this.props.data.dbhomedir,
            dblocks: this.props.data.dblocks,
            dblocksMonitoring: this.props.data.dblocksMonitoring,
            dblocksMonitoringThreshold: this.props.data.dblocksMonitoringThreshold,
            dblocksMonitoringPause: this.props.data.dblocksMonitoringPause,
            chxpoint: this.props.data.chxpoint,
            compactinterval: this.props.data.compactinterval,
            compacttime: this.props.data.compacttime,
            importcachesize: this.props.data.importcachesize,
            importcacheauto: this.props.data.importcacheauto,
            // These variables store the original value (used for saving config)
            _looklimit: this.props.data.looklimit,
            _idscanlimit: this.props.data.idscanlimit,
            _pagelooklimit: this.props.data.pagelooklimit,
            _pagescanlimit: this.props.data.pagescanlimit,
            _rangelooklimit: this.props.data.rangelooklimit,
            _autosize: this.props.data.autosize,
            _autosizesplit: this.props.data.autosizesplit,
            _dbcachesize: this.props.data.dbcachesize,
            _txnlogdir: this.props.data.txnlogdir,
            _dbhomedir: this.props.data.dbhomedir,
            _dblocks: this.props.data.dblocks,
            _dblocksMonitoring: this.props.data.dblocksMonitoring,
            _dblocksMonitoringThreshold: this.props.data.dblocksMonitoringThreshold,
            _dblocksMonitoringPause: this.props.data.dblocksMonitoringPause,
            _chxpoint: this.props.data.chxpoint,
            _compactinterval: this.props.data.compactinterval,
            _compacttime: this.props.data.compacttime,
            _importcachesize: this.props.data.importcachesize,
            _importcacheauto: this.props.data.importcacheauto,
            _db_cache_auto: this.props.data.db_cache_auto,
            _import_cache_auto: this.props.data.import_cache_auto,
        };

        this.handleChange = this.handleChange.bind(this);
        this.select_db_locks_monitoring = this.select_db_locks_monitoring.bind(this);
        this.save_db_config = this.save_db_config.bind(this);

        this.onToggle = (isExpanded) => {
            this.setState({
                isExpanded
            });
        };
    }

    componentDidMount() {
        this.props.enableTree();
    }

    select_db_locks_monitoring (val, e) {
        this.setState({
            dblocksMonitoring: !this.state.dblocksMonitoring
        }, this.handleChange(val, e));
    }

    handleChange(str, e) {
        // Generic
        let saveBtnDisabled = true;
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        const check_attrs = [
            "db_cache_auto", "import_cache_auto", "looklimit",
            "idscanlimit", "pagelooklimit", "pagescanlimit",
            "rangelooklimit", "autosize", "autosizesplit",
            "dbcachesize", "txnlogdir", "dbhomedir",
            "dblocks", "dblocksMonitoring", "dblocksMonitoringThreshold",
            "dblocksMonitoringPause", "chxpoint", "compactinterval",
            "compacttime", "importcachesize", "importcacheauto",
        ];
        for (let check_attr of check_attrs) {
            if (attr != check_attr) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value != this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled: saveBtnDisabled
        });
    }

    save_db_config() {
        // Build up the command list
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'backend', 'config', 'set'
        ];
        let requireRestart = false;

        if (this.state._looklimit != this.state.looklimit) {
            cmd.push("--lookthroughlimit=" + this.state.looklimit);
        }
        if (this.state._idscanlimit != this.state.idscanlimit) {
            cmd.push("--idlistscanlimit=" + this.state.idscanlimit);
        }
        if (this.state._pagelooklimit != this.state.pagelooklimit) {
            cmd.push("--pagedlookthroughlimit=" + this.state.pagelooklimit);
        }
        if (this.state._pagescanlimit != this.state.pagescanlimit) {
            cmd.push("--pagedidlistscanlimit=" + this.state.pagescanlimit);
        }
        if (this.state._rangelooklimit != this.state.rangelooklimit) {
            cmd.push("--rangelookthroughlimit=" + this.state.rangelooklimit);
        }
        if (this.state.db_cache_auto) {
            // Auto cache is selected
            if (this.state._db_cache_auto != this.state.db_cache_auto) {
                // We just enabled auto cache,
                if (this.state.autosize == "0") {
                    cmd.push("--cache-autosize=10");
                } else {
                    cmd.push("--cache-autosize=" + this.state.autosize);
                }
                requireRestart = true;
            } else if (this.state._autosize != this.state.autosize) {
                // Update auto cache settings if it changed
                cmd.push("--cache-autosize=" + this.state.autosize);
                requireRestart = true;
            }
        } else {
            // No auto cache, check if we need to reset the value
            if (this.state._db_cache_auto != this.state.db_cache_auto) {
                // We just disabled auto cache
                cmd.push("--cache-autosize=0");
                requireRestart = true;
            }
        }
        if (this.state._autosizesplit != this.state.autosizesplit) {
            cmd.push("--cache-autosize-split=" + this.state.autosizesplit);
            requireRestart = true;
        }
        if (this.state._dbcachesize != this.state.dbcachesize) {
            cmd.push("--dbcachesize=" + this.state.dbcachesize);
            requireRestart = true;
        }
        if (this.state._txnlogdir != this.state.txnlogdir) {
            cmd.push("--logdirectory=" + this.state.txnlogdir);
            requireRestart = true;
        }
        if (this.state._dbhomedir != this.state.dbhomedir) {
            cmd.push("--db-home-directory=" + this.state.dbhomedir);
            requireRestart = true;
        }
        if (this.state._dblocks != this.state.dblocks) {
            cmd.push("--locks=" + this.state.dblocks);
            requireRestart = true;
        }
        if (this.state._dblocksMonitoring != this.state.dblocksMonitoring) {
            if (this.state.dblocksMonitoring) {
                cmd.push("--locks-monitoring-enabled=on");
            } else {
                cmd.push("--locks-monitoring-enabled=off");
            }
            requireRestart = true;
        }
        if (this.state._dblocksMonitoringThreshold != this.state.dblocksMonitoringThreshold) {
            cmd.push("--locks-monitoring-threshold=" + this.state.dblocksMonitoringThreshold);
            requireRestart = true;
        }
        if (this.state._dblocksMonitoringPause != this.state.dblocksMonitoringPause) {
            cmd.push("--locks-monitoring-pause=" + this.state.dblocksMonitoringPause);
        }
        if (this.state._chxpoint != this.state.chxpoint) {
            cmd.push("--checkpoint-interval=" + this.state.chxpoint);
            requireRestart = true;
        }
        if (this.state._compactinterval != this.state.compactinterval) {
            cmd.push("--compactdb-interval=" + this.state.compactinterval);
            requireRestart = true;
        }
        if (this.state._compacttime != this.state.compacttime) {
            cmd.push("--compactdb-time=" + this.state.compacttime);
            requireRestart = true;
        }
        if (this.state.import_cache_auto) {
            // Auto cache is selected
            if (this.state._import_cache_auto != this.state.import_cache_auto) {
                // We just enabled auto cache,
                if (this.state.importcachesize == "0") {
                    cmd.push("--import-cache-autosize=-1");
                } else {
                    cmd.push("--import-cache-autosize=" + this.state.importcachesize);
                }
            } else if (this.state._importcachesize != this.state.importcachesize) {
                // Update auto cache settings if it changed
                cmd.push("--import-cache-autosize=" + this.state.importcachesize);
            }
        } else {
            // Auto cache is not selected, check if we need to reset the value
            if (this.state._import_cache_auto != this.state.import_cache_auto) {
                // We just disabled auto cache
                cmd.push("--import-cache-autosize=0");
            }
        }
        if (this.state._importcachesize != this.state.importcachesize) {
            cmd.push("--import-cachesize=" + this.state.importcachesize);
        }
        if (cmd.length > 6) {
            this.setState({
                saving: true
            });
            log_cmd("save_db_config", "Applying config change", cmd);
            let msg = "Successfully updated database configuration";
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload();
                        this.setState({
                            saving: false
                        });
                        if (requireRestart) {
                            this.props.addNotification(
                                "warning",
                                msg + ". You must restart the Directory Server for these changes to take effect."
                            );
                        }
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.reload();
                        this.setState({
                            saving: false
                        });
                        this.props.addNotification(
                            "error",
                            `Error updating configuration - ${errMsg.desc}`
                        );
                    });
        }
    }

    render() {
        let db_cache_form;
        let import_cache_form;
        let db_auto_checked = false;
        let import_auto_checked = false;
        let dblocksMonitor = "";
        let dblocksThreshold = this.state.dblocksMonitoringThreshold;
        let dblocksPause = this.state.dblocksMonitoringPause;

        if (this.state.dblocksMonitoring) {
            dblocksMonitor =
                <div className="ds-margin-left ds-margin-top">
                    <Grid
                        title="Sets the DB lock exhaustion value in percentage (valid range is 70-95). If too many locks are acquired, the server will abort the searches while the number of locks are not decreased. It helps to avoid DB corruption and long recovery. (nsslapd-db-locks-monitoring-threshold)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            DB Locks Threshold Percentage
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                id="dblocksMonitoringThreshold"
                                name="dblocksMonitoringThreshold"
                                type="number"
                                aria-describedby="dblocksMonitoringThreshold"
                                value={dblocksThreshold}
                                onChange={this.handleChange}
                                validated={parseInt(dblocksThreshold) < 70 || parseInt(dblocksThreshold) > 95 ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Sets the amount of time (milliseconds) that the monitoring thread spends waiting between checks. (nsslapd-db-locks-monitoring-pause)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            DB Locks Pause Milliseconds
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                id="dblocksMonitoringPause"
                                name="dblocksMonitoringPause"
                                type="number"
                                aria-describedby="dblocksMonitoringPause"
                                value={dblocksPause}
                                onChange={this.handleChange}
                                validated={parseInt(dblocksPause) < 1 ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                </div>;
        }

        if (this.state.db_cache_auto) {
            db_cache_form =
                <div className="ds-margin-left">
                    <Grid
                        title="Enable database and entry cache auto-tuning using a percentage of the system's current resources (nsslapd-cache-autosize)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={6}>
                            Memory Percentage
                        </GridItem>
                        <GridItem span={6}>
                            <TextInput
                                value={this.state.autosize}
                                type="number"
                                id="autosize"
                                aria-describedby="autosize"
                                name="autosize"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Sets the percentage of memory that is used for the database cache. The remaining percentage is used for the entry cache (nsslapd-cache-autosize-split)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={6}>
                            DB Cache Percentage
                        </GridItem>
                        <GridItem span={6}>
                            <TextInput
                                value={this.state.autosizesplit}
                                type="number"
                                id="autosizesplit"
                                aria-describedby="autosizesplit"
                                name="autosizesplit"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                </div>;
            db_auto_checked = true;
        } else {
            db_cache_form = <div className="ds-margin-left">
                <Grid
                    title="Specifies the database index cache size in bytes (nsslapd-dbcachesize)."
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={6}>
                        Database Cache Size
                    </GridItem>
                    <GridItem span={6}>
                        <TextInput
                            value={this.state.dbcachesize}
                            type="number"
                            id="dbcachesize"
                            aria-describedby="dbcachesize"
                            name="dbcachesize"
                            onChange={this.handleChange}
                        />
                    </GridItem>
                </Grid>
            </div>;
            db_auto_checked = false;
        }

        if (this.state.import_cache_auto) {
            import_cache_form =
                <div id="auto-import-cache-form" className="ds-margin-left">
                    <Grid
                        title="Enter '-1' to use 50% of available memory, '0' to disable autotuning, or enter the percentage of available memory to use.  Value range -1 through 100, default is '-1' (nsslapd-import-cache-autosize)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={6}>
                            Import Cache Autosize
                        </GridItem>
                        <GridItem span={6}>
                            <TextInput
                                value={this.state.importcacheauto}
                                type="number"
                                id="importcacheauto"
                                aria-describedby="importcacheauto"
                                name="importcacheauto"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                </div>;
            import_auto_checked = true;
        } else {
            import_cache_form =
                <div className="ds-margin-left">
                    <Grid
                        title="The size of the database cache in bytes used in the bulk import process. (nsslapd-import-cachesize)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={6}>
                            Import Cache Size
                        </GridItem>
                        <GridItem span={6}>
                            <TextInput
                                value={this.state.importcachesize}
                                type="number"
                                id="importcachesize"
                                aria-describedby="importcachesize"
                                name="importcachesize"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                </div>;
            import_auto_checked = false;
        }

        let spinner = "";
        if (this.state.loading) {
            spinner =
                <div className="ds-loading-spinner ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading global database configuration ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" loading size="md" />
                </div>;
        }

        let saveBtnName = "Save Config";
        let extraPrimaryProps = {};
        if (this.props.refreshing) {
            saveBtnName = "Saving config ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <div className={this.state.saving ? "ds-disabled" : ""} id="db-global-page">
                {spinner}
                <div className={this.state.loading ? 'ds-fadeout' : 'ds-fadein'}>
                    <TextContent>
                        <Text className="ds-config-header" component={TextVariants.h2}>
                            Global Database Configuration
                        </Text>
                    </TextContent>
                    <Grid
                        title="The maximum number of entries that the Directory Server will check when examining candidate entries in response to a search request (nsslapd-lookthrough-limit)."
                        className="ds-margin-top-lg"
                    >
                        <GridItem className="ds-label" span={3}>
                            Database Look Though Limit
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state.looklimit}
                                type="number"
                                id="looklimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="looklimit"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The number of entry IDs that are searched during a search operation (nsslapd-idlistscanlimit)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            ID List Scan Limit
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state.idscanlimit}
                                type="number"
                                id="idscanlimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="idscanlimit"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The maximum number of entries that the Directory Server will check when examining candidate entries for a search which uses the simple paged results control (nsslapd-pagedlookthroughlimit)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Paged Search Look Through Limit
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state.pagelooklimit}
                                type="number"
                                id="pagelooklimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="pagelooklimit"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The number of entry IDs that are searched, specifically, for a search operation using the simple paged results control (nsslapd-pagedidlistscanlimit)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Paged Search ID List Scan Limit
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state.pagescanlimit}
                                type="number"
                                id="pagescanlimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="pagescanlimit"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The maximum number of entries that the Directory Server will check when examining candidate entries in response to a range search request (nsslapd-rangelookthroughlimit)."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Range Search Look Through Limit
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state.rangelooklimit}
                                type="number"
                                id="rangelooklimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="rangelooklimit"
                                onChange={this.handleChange}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={6}>
                            <TextContent>
                                <Text className="ds-sub-header" component={TextVariants.h3}>
                                    Database Cache Settings
                                </Text>
                            </TextContent>
                            <hr />
                        </GridItem>
                        <GridItem span={6}>
                            <TextContent>
                                <Text className="ds-sub-header" component={TextVariants.h3}>
                                    Import Cache Settings
                                </Text>
                            </TextContent>
                            <hr />
                        </GridItem>

                        <GridItem span={6}>
                            <Checkbox
                                label="Automatic Cache Tuning"
                                onChange={this.handleChange}
                                isChecked={db_auto_checked}
                                aria-label="uncontrolled checkbox example"
                                id="db_cache_auto"
                            />
                        </GridItem>
                        <GridItem span={6}>
                            <Checkbox
                                label="Automatic Import Cache Tuning"
                                title="Set import cache to be set automatically"
                                onChange={this.handleChange}
                                isChecked={import_auto_checked}
                                aria-label="uncontrolled checkbox example"
                                id="import_cache_auto"
                            />
                        </GridItem>

                        <GridItem span={5}>
                            {db_cache_form}
                        </GridItem>
                        <GridItem span={1} />
                        <GridItem span={5}>
                            {import_cache_form}
                        </GridItem>
                    </Grid>

                    <ExpandableSection
                        className="ds-margin-top-xlg"
                        toggleText={this.state.isExpanded ? 'Hide Advanced Settings' : 'Show Advanced Settings'}
                        onToggle={this.onToggle}
                        isExpanded={this.state.isExpanded}
                    >
                        <div className="ds-left-indent-md">
                            <Grid
                                title="Database Transaction Log Location (nsslapd-db-logdirectory)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Transaction Logs Directory
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.txnlogdir}
                                        type="text"
                                        id="txnlogdir"
                                        aria-describedby="txnlogdir"
                                        name="txnlogdir"
                                        onChange={this.handleChange}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Location for database memory mapped files.  You must specify a subdirectory of a tempfs type filesystem (nsslapd-db-home-directory)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Database Home Directory
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.dbhomedir}
                                        type="text"
                                        id="dbhomedir"
                                        aria-describedby="dbhomedir"
                                        name="dbhomedir"
                                        onChange={this.handleChange}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Amount of time in seconds after which the Directory Server sends a checkpoint entry to the database transaction log (nsslapd-db-checkpoint-interval)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Database Checkpoint Interval
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.chxpoint}
                                        type="number"
                                        id="chxpoint"
                                        aria-describedby="chxpoint"
                                        name="chxpoint"
                                        onChange={this.handleChange}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The interval in seconds when the database is compacted (nsslapd-db-compactdb-interval).  The default is 30 days at midnight."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Database Compaction Interval
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.compactinterval}
                                        type="number"
                                        id="compactinterval"
                                        aria-describedby="compactinterval"
                                        name="compactinterval"
                                        onChange={this.handleChange}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The Time Of Day to perform the database compaction after the compact interval has been met.  Uses the format: 'HH:MM' and defaults to '23:59'. (nsslapd-db-compactdb-time)"
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Database Compaction Time
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.compacttime}
                                        type="text"
                                        id="compacttime"
                                        aria-describedby="compacttime"
                                        name="compacttime"
                                        onChange={this.handleChange}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The number of database locks (nsslapd-db-locks)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Database Compaction Time
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.dblocks}
                                        type="number"
                                        id="dblocks"
                                        aria-describedby="dblocks"
                                        name="dblocks"
                                        onChange={this.handleChange}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-top-xlg">
                                <GridItem span={12}>
                                    <TextContent>
                                        <Text className="ds-sub-header" component={TextVariants.h3}>
                                            DB Locks Monitoring
                                        </Text>
                                    </TextContent>
                                    <hr />
                                </GridItem>
                                <GridItem span={12}>
                                    <Checkbox
                                        label="Enable Monitoring"
                                        id="dblocksMonitoring"
                                        isChecked={this.state.dblocksMonitoring}
                                        onChange={this.select_db_locks_monitoring}
                                        aria-label="uncontrolled checkbox example"
                                    />
                                </GridItem>
                                <GridItem span={12}>
                                    {dblocksMonitor}
                                </GridItem>
                            </Grid>
                        </div>
                    </ExpandableSection>
                    <Button
                        className="ds-margin-top-lg"
                        onClick={this.save_db_config}
                        variant="primary"
                        isLoading={this.state.saving}
                        spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.state.saveBtnDisabled}
                    >
                        {saveBtnName}
                    </Button>
                    <hr />
                </div>
            </div>
        );
    }
}

// Property types and defaults

GlobalDatabaseConfig.propTypes = {
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    data: PropTypes.object,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

GlobalDatabaseConfig.defaultProps = {
    serverId: "",
    addNotification: noop,
    data: {},
    reload: noop,
    enableTree: noop,
};
