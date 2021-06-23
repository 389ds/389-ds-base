import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    ExpandableSection,
    Button,
    Checkbox,
    Form,
    FormGroup,
    TextInput,
    Spinner,
    Grid,
    GridItem,
    ValidatedOptions,
    noop
} from "@patternfly/react-core";

import PropTypes from "prop-types";

export class GlobalDatabaseConfig extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            isExpanded: false,
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
        this.select_auto_cache = this.select_auto_cache.bind(this);
        this.select_auto_import_cache = this.select_auto_import_cache.bind(this);
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

    select_auto_cache (val, e) {
        this.setState({
            db_cache_auto: !this.state.db_cache_auto
        }, this.handleChange(val, e));
    }

    select_auto_import_cache (val, e) {
        this.setState({
            import_cache_auto: !this.state.import_cache_auto
        }, this.handleChange(val, e));
    }

    select_db_locks_monitoring (val, e) {
        this.setState({
            dblocksMonitoring: !this.state.dblocksMonitoring
        }, this.handleChange(val, e));
    }

    handleChange(str, e) {
        // Generic
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
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
            log_cmd("save_db_config", "Applying config change", cmd);
            let msg = "Successfully updated database configuration";
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload();
                        this.props.addNotification(
                            "success",
                            msg
                        );
                        if (requireRestart) {
                            this.props.addNotification(
                                "warning",
                                `You must restart the Directory Server for these changes to take effect.`
                            );
                        }
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.reload();
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
                    <FormGroup
                        label="DB Locks Threshold Percentage"
                        fieldId="monitoringthreshold"
                        title="Sets the DB lock exhaustion threshold in percentage (valid range is 70-95). When the threshold is reached, all searches are aborted until the number of active locks decreases below the configured threshold and/or the directory server administrator increases the number of Database Locks (nsslapd-db-locks). This threshold is a safeguard against DB corruption which might be caused by locks exhaustion. (nsslapd-db-locks-monitoring-threshold) ('90' by default)"
                    >
                        <TextInput
                            id="dblocksMonitoringThreshold"
                            name="dblocksMonitoringThreshold"
                            type="number"
                            aria-describedby="dblocksMonitoringThreshold"
                            value={dblocksThreshold}
                            onChange={this.handleChange}
                            validated={parseInt(dblocksThreshold) < 70 || parseInt(dblocksThreshold) > 95 ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </FormGroup>
                    <FormGroup
                        label="DB Locks Pause Milliseconds"
                        fieldId="monitoringpause"
                        title="Sets the amount of time (milliseconds) that the DB lock monitoring thread spends waiting between checks. (nsslapd-db-locks-monitoring-pause) ('500' by default)"
                    >
                        <TextInput
                            id="dblocksMonitoringPause"
                            name="dblocksMonitoringPause"
                            type="number"
                            aria-describedby="dblocksMonitoringPause"
                            value={dblocksPause}
                            onChange={this.handleChange}
                            validated={parseInt(dblocksPause) < 1 ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </FormGroup>
                </div>;
        }

        if (this.state.db_cache_auto) {
            db_cache_form = <div className="ds-margin-left">
                <Form isHorizontal>
                    <FormGroup
                        label="Memory Percentage"
                        fieldId="autosize"
                        title="Enable database and entry cache auto-tuning using a percentage of the system's current resources (nsslapd-cache-autosize)."
                    >
                        <TextInput
                            value={this.state.autosize}
                            type="text"
                            id="autosize"
                            aria-describedby="autosize"
                            name="autosize"
                            onChange={this.handleChange}
                        />
                    </FormGroup>
                    <FormGroup
                        label="DB Cache Percentage"
                        fieldId="autosizesplit"
                        title="Sets the percentage of memory that is used for the database cache. The remaining percentage is used for the entry cache (nsslapd-cache-autosize-split)."
                    >
                        <TextInput
                            value={this.state.autosizesplit}
                            type="text"
                            id="autosizesplit"
                            aria-describedby="autosizesplit"
                            name="autosizesplit"
                            onChange={this.handleChange}
                        />
                    </FormGroup>
                </Form>
            </div>;
            db_auto_checked = true;
        } else {
            db_cache_form = <div className="ds-margin-left">
                <Form isHorizontal>
                    <FormGroup
                        label="Database Cache Size"
                        fieldId="dbcachesize"
                        title="Specifies the database index cache size in bytes (nsslapd-dbcachesize)."
                    >
                        <TextInput
                            value={this.state.dbcachesize}
                            type="text"
                            id="dbcachesize"
                            aria-describedby="dbcachesize"
                            name="dbcachesize"
                            onChange={this.handleChange}
                        />
                    </FormGroup>
                </Form>
            </div>;
            db_auto_checked = false;
        }

        if (this.state.import_cache_auto) {
            import_cache_form = <div id="auto-import-cache-form" className="ds-margin-left">
                <Form isHorizontal>
                    <FormGroup
                        label="Import Cache Autosize"
                        fieldId="importcacheauto"
                        title="Enter '-1' to use 50% of available memory, '0' to disable autotuning, or enter the percentage of available memory to use.  Value range -1 through 100, default is '-1' (nsslapd-import-cache-autosize)."
                    >
                        <TextInput
                            value={this.state.importcacheauto}
                            type="text"
                            id="importcacheauto"
                            aria-describedby="importcacheauto"
                            name="importcacheauto"
                            onChange={this.handleChange}
                        />
                    </FormGroup>
                </Form>
            </div>;
            import_auto_checked = true;
        } else {
            import_cache_form = <div className="ds-margin-left">
                <Form isHorizontal>
                    <FormGroup
                        label="Import Cache Size"
                        fieldId="importcachesize"
                        title="The size of the database cache in bytes used in the bulk import process. (nsslapd-import-cachesize)."
                    >
                        <TextInput
                            value={this.state.importcachesize}
                            type="text"
                            id="importcachesize"
                            aria-describedby="importcachesize"
                            name="importcachesize"
                            onChange={this.handleChange}
                        />
                    </FormGroup>
                </Form>
            </div>;
            import_auto_checked = false;
        }

        let spinner = "";
        if (this.state.loading) {
            spinner =
                <div className="ds-loading-spinner ds-margin-top ds-center">
                    <h4>Loading global database configuration ...</h4>
                    <Spinner className="ds-margin-top" loading size="md" />
                </div>;
        }

        return (
            <div id="db-global-page">
                {spinner}
                <div className={this.state.loading ? 'ds-fadeout' : 'ds-fadein'}>
                    <h3 className="ds-config-header">Global Database Configuration</h3>
                    <Form isHorizontal>
                        <FormGroup
                            label="Database Look Though Limit"
                            fieldId="lookthrough"
                            title="The maximum number of entries that the Directory Server will check when examining candidate entries in response to a search request (nsslapd-lookthrough-limit)."
                        >
                            <TextInput
                                value={this.state.looklimit}
                                type="text"
                                id="looklimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="looklimit"
                                onChange={this.handleChange}
                            />
                        </FormGroup>
                        <FormGroup
                            label="ID List Scan Limit"
                            fieldId="idscan"
                            title="The number of entry IDs that are searched during a search operation (nsslapd-idlistscanlimit)."
                        >
                            <TextInput
                                value={this.state.idscanlimit}
                                type="text"
                                id="idscanlimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="idscanlimit"
                                onChange={this.handleChange}
                            />
                        </FormGroup>
                        <FormGroup
                            label="Paged Search Look Through Limit"
                            fieldId="pagedsearch"
                            title="The maximum number of entries that the Directory Server will check when examining candidate entries for a search which uses the simple paged results control (nsslapd-pagedlookthroughlimit)."
                        >
                            <TextInput
                                value={this.state.pagelooklimit}
                                type="text"
                                id="pagelooklimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="pagelooklimit"
                                onChange={this.handleChange}
                            />
                        </FormGroup>
                        <FormGroup
                            label="Paged Search ID List Scan Limit"
                            fieldId="pagedscan"
                            title="The number of entry IDs that are searched, specifically, for a search operation using the simple paged results control (nsslapd-pagedidlistscanlimit)."
                        >
                            <TextInput
                                value={this.state.pagescanlimit}
                                type="text"
                                id="pagescanlimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="pagescanlimit"
                                onChange={this.handleChange}
                            />
                        </FormGroup>
                        <FormGroup
                            label="Range Search Look Through Limit"
                            fieldId="pagedscan"
                            title="The maximum number of entries that the Directory Server will check when examining candidate entries in response to a range search request (nsslapd-rangelookthroughlimit)."
                        >
                            <TextInput
                                value={this.state.rangelooklimit}
                                type="text"
                                id="rangelooklimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="rangelooklimit"
                                onChange={this.handleChange}
                            />
                        </FormGroup>
                    </Form>

                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={6}>
                            <h4 className="ds-sub-header">Database Cache Settings</h4>
                            <hr />
                        </GridItem>
                        <GridItem span={6}>
                            <h4 className="ds-sub-header">Import Cache Settings</h4>
                            <hr />
                        </GridItem>

                        <GridItem span={6}>
                            <Checkbox
                                label="Automatic Cache Tuning"
                                onChange={this.select_auto_cache}
                                isChecked={db_auto_checked}
                                aria-label="uncontrolled checkbox example"
                                id="autoCacheChkbox"
                            />
                        </GridItem>
                        <GridItem span={6}>
                            <Checkbox
                                label="Automatic Import Cache Tuning"
                                title="Set import cache to be set automatically"
                                onChange={this.select_auto_import_cache}
                                isChecked={import_auto_checked}
                                aria-label="uncontrolled checkbox example"
                                id="autoImportCacheChkbox"
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
                        <Form className="ds-indent" isHorizontal>
                            <FormGroup
                                label="Transaction Logs Directory"
                                fieldId="txnlogdir"
                                title="Database Transaction Log Location (nsslapd-db-logdirectory)."
                            >
                                <TextInput
                                    value={this.state.txnlogdir}
                                    type="text"
                                    id="txnlogdir"
                                    aria-describedby="txnlogdir"
                                    name="txnlogdir"
                                    onChange={this.handleChange}
                                />
                            </FormGroup>
                            <FormGroup
                                label="Database Home Directory"
                                fieldId="dbhomedir"
                                title="Location for database memory mapped files.  You must specify a subdirectory of a tempfs type filesystem (nsslapd-db-home-directory)."
                            >
                                <TextInput
                                    value={this.state.dbhomedir}
                                    type="text"
                                    id="dbhomedir"
                                    aria-describedby="dbhomedir"
                                    name="dbhomedir"
                                    onChange={this.handleChange}
                                />
                            </FormGroup>
                            <FormGroup
                                label="Database Checkpoint Interval"
                                fieldId="chxpoint"
                                title="Amount of time in seconds after which the Directory Server sends a checkpoint entry to the database transaction log (nsslapd-db-checkpoint-interval)."
                            >
                                <TextInput
                                    value={this.state.chxpoint}
                                    type="text"
                                    id="chxpoint"
                                    aria-describedby="chxpoint"
                                    name="chxpoint"
                                    onChange={this.handleChange}
                                />
                            </FormGroup>
                            <FormGroup
                                label="Database Compact Interval"
                                fieldId="compactinterval"
                                title="The interval in seconds when the database is compacted (nsslapd-db-compactdb-interval).  The default is 30 days at midnight."
                            >
                                <TextInput
                                    value={this.state.compactinterval}
                                    type="number"
                                    id="compactinterval"
                                    aria-describedby="compactinterval"
                                    name="compactinterval"
                                    onChange={this.handleChange}
                                />
                            </FormGroup>
                            <FormGroup
                                label="Database Compact Time"
                                fieldId="compacttime"
                                title="The Time Of Day to perform the database compaction after the compact interval has been met.  Uses the format: 'HH:MM' and defaults to '23:59'. (nsslapd-db-compactdb-time)"
                            >
                                <TextInput
                                    value={this.state.compacttime}
                                    type="text"
                                    id="compacttime"
                                    aria-describedby="compacttime"
                                    name="compacttime"
                                    onChange={this.handleChange}
                                />
                            </FormGroup>
                            <FormGroup
                                label="Database Locks"
                                fieldId="dblocks"
                                title="The number of database locks (nsslapd-db-locks)."
                            >
                                <TextInput
                                    value={this.state.dblocks}
                                    type="text"
                                    id="dblocks"
                                    aria-describedby="dblocks"
                                    name="dblocks"
                                    onChange={this.handleChange}
                                />
                            </FormGroup>
                            <Grid className="ds-margin-top-xlg">
                                <GridItem span={12}>
                                    <h5 className="ds-sub-header">DB Locks Monitoring</h5>
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
                        </Form>
                    </ExpandableSection>
                    <hr />
                    <Button className="save-button" onClick={this.save_db_config} variant="primary">
                        Save Configuration
                    </Button>
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
