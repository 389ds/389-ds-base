import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    ExpandableSection,
    Grid,
    GridItem,
    NumberInput,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    TimePicker,
    Tooltip,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import OutlinedQuestionCircleIcon from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';

export class GlobalDatabaseConfig extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            saving: false,
            saveBtnDisabled: true,
            activeTabKey:  this.props.data.activeTab,
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
            ndncachemaxsize: this.props.data.ndncachemaxsize,
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
            _ndncachemaxsize: this.props.data.ndncachemaxsize,
        };

        this.validateSaveBtn = this.validateSaveBtn.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
        this.select_db_locks_monitoring = this.select_db_locks_monitoring.bind(this);
        this.save_db_config = this.save_db_config.bind(this);

        this.maxValue = 2147483647;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validateSaveBtn() });
        };
        this.onConfigChange = (event, id, min, max) => {
            let maxValue = this.maxValue;
            if (max !== 0) {
                maxValue = max;
            }
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > maxValue ? maxValue : newValue < min ? min : newValue
            }, () => { this.validateSaveBtn() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateSaveBtn() });
        }

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
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

    validateSaveBtn() {
        let saveBtnDisabled = true;
        const check_attrs = [
            "db_cache_auto", "import_cache_auto", "looklimit",
            "idscanlimit", "pagelooklimit", "pagescanlimit",
            "rangelooklimit", "autosize", "autosizesplit",
            "dbcachesize", "txnlogdir", "dbhomedir",
            "dblocks", "dblocksMonitoring", "dblocksMonitoringThreshold",
            "dblocksMonitoringPause", "chxpoint", "compactinterval",
            "compacttime", "importcachesize", "importcacheauto",
            "ndncachemaxsize",
        ];

        // Check if a setting was changed, if so enable the save button
        for (const config_attr of check_attrs) {
            if (this.state[config_attr] != this.state['_' + config_attr]) {
                saveBtnDisabled = false;
                break;
            }
        }
        this.setState({
            saveBtnDisabled: saveBtnDisabled,
        });
    }

    handleChange(str, e) {
        // Generic
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateSaveBtn() });
    }

    handleTimeChange(value) {
        this.setState({
            compacttime: value,
        }, () => { this.validateSaveBtn() });
    }

    save_ndn_cache(requireRestart) {
        const msg = "Successfully updated database configuration";
        if (this.state._ndncachemaxsize != this.state.ndncachemaxsize) {
            const cmd = [
                'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
                'config', 'replace', 'nsslapd-ndn-cache-max-size=' + this.state.ndncachemaxsize
            ];

            log_cmd("save_ndn_cache", "Applying config change", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.props.reload(this.state.activeTabKey);
                        this.setState({
                            saving: false
                        });
                        if (requireRestart) {
                            this.props.addNotification(
                                "warning",
                                msg + ". You must restart the Directory Server for these changes to take effect."
                            );
                        } else {
                            this.props.addNotification(
                                "success",
                                msg
                            );
                        }
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.reload(this.state.activeTabKey);
                        this.setState({
                            saving: false
                        });
                        this.props.addNotification(
                            "error",
                            `Error updating configuration - ${errMsg.desc}`
                        );
                    });
        } else {
            this.props.reload(this.state.activeTabKey);
            this.setState({
                saving: false
            });
            if (requireRestart) {
                this.props.addNotification(
                    "warning",
                    msg + ". You must restart the Directory Server for these changes to take effect."
                );
            } else {
                this.props.addNotification(
                    "success",
                    msg
                );
            }
        }
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
                    cmd.push("--import-cache-autosize=" + this.state.importcacheauto);
                }
            } else if (this.state._importcacheauto != this.state.importcacheauto) {
                // Update auto cache settings if it changed
                cmd.push("--import-cache-autosize=" + this.state.importcacheauto);
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
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        // Continue with the next mod
                        this.save_ndn_cache(requireRestart);
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.reload(this.state.activeTabKey);
                        this.setState({
                            saving: false
                        });
                        this.props.addNotification(
                            "error",
                            `Error updating configuration - ${errMsg.desc}`
                        );
                    });
        } else {
            this.setState({
                saving: true
            }, () => { this.save_ndn_cache(requireRestart) });
        }
    }

    render() {
        let db_cache_form;
        let import_cache_form;
        let db_auto_checked = false;
        let import_auto_checked = false;
        let dblocksMonitor = "";
        const dblocksThreshold = this.state.dblocksMonitoringThreshold;
        const dblocksPause = this.state.dblocksMonitoringPause;

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
                            <NumberInput
                                value={dblocksThreshold}
                                min={70}
                                max={95}
                                onMinus={() => { this.onMinusConfig("dblocksMonitoringThreshold") }}
                                onChange={(e) => { this.onConfigChange(e, "dblocksMonitoringThreshold", 70, 95) }}
                                onPlus={() => { this.onPlusConfig("dblocksMonitoringThreshold") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={10}
                                unit="%"
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
                            <NumberInput
                                value={dblocksPause}
                                min={0}
                                max={this.maxValue}
                                onMinus={() => { this.onMinusConfig("dblocksMonitoringPause") }}
                                onChange={(e) => { this.onConfigChange(e, "dblocksMonitoringPause", 0, 0) }}
                                onPlus={() => { this.onPlusConfig("dblocksMonitoringPause") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={10}
                            />
                        </GridItem>
                    </Grid>
                </div>;
        }

        if (this.state.db_cache_auto) {
            db_cache_form =
                <div className="ds-margin-left">
                    <Grid
                        title="Enable database and entry cache auto-tuning using a percentage of the system's current resources (nsslapd-cache-autosize). If 0 is set, the default value is used instead."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Memory Percentage
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={this.state.autosize}
                                min={0}
                                max={100}
                                onMinus={() => { this.onMinusConfig("autosize") }}
                                onChange={(e) => { this.onConfigChange(e, "autosize", 0, 100) }}
                                onPlus={() => { this.onPlusConfig("autosize") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={4}
                                unit="%"
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Sets the percentage of memory that is used for the database cache. The remaining percentage is used for the entry cache (nsslapd-cache-autosize-split). If 0 is set, the default value is used instead."
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            DB Cache Percentage
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={this.state.autosizesplit}
                                min={1}
                                max={99}
                                onMinus={() => { this.onMinusConfig("autosizesplit") }}
                                onChange={(e) => { this.onConfigChange(e, "autosizesplit", 1, 99) }}
                                onPlus={() => { this.onPlusConfig("autosizesplit") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={4}
                                unit="%"
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
                    <GridItem className="ds-label" span={3}>
                        Database Cache Size
                    </GridItem>
                    <GridItem span={9}>
                        <NumberInput
                            value={this.state.dbcachesize}
                            min={512000}
                            max={this.maxValue}
                            onMinus={() => { this.onMinusConfig("dbcachesize") }}
                            onChange={(e) => { this.onConfigChange(e, "dbcachesize", 512000, 0) }}
                            onPlus={() => { this.onPlusConfig("dbcachesize") }}
                            inputName="input"
                            inputAriaLabel="number input"
                            minusBtnAriaLabel="minus"
                            plusBtnAriaLabel="plus"
                            widthChars={10}
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
                        <GridItem className="ds-label" span={3}>
                            Import Cache Autosize
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={this.state.importcacheauto}
                                min={-1}
                                max={100}
                                onMinus={() => { this.onMinusConfig("importcacheauto") }}
                                onChange={(e) => { this.onConfigChange(e, "importcacheauto", -1, 100) }}
                                onPlus={() => { this.onPlusConfig("importcacheauto") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={4}
                                unit={this.state.importcacheauto > 0 ? "%" : ""}
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
                        <GridItem className="ds-label" span={3}>
                            Import Cache Size
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={this.state.importcachesize}
                                min={512000}
                                max={this.maxValue}
                                onMinus={() => { this.onMinusConfig("importcachesize") }}
                                onChange={(e) => { this.onConfigChange(e, "importcachesize", 512000, 0) }}
                                onPlus={() => { this.onPlusConfig("importcachesize") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={10}
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
        const extraPrimaryProps = {};
        if (this.props.refreshing) {
            saveBtnName = "Saving config ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <div className={this.state.saving ? "ds-disabled ds-margin-bottom-md" : "ds-margin-bottom-md"} id="db-global-page">
                {spinner}
                <div className={this.state.loading ? 'ds-fadeout' : 'ds-fadein'}>
                    <TextContent>
                        <Text className="ds-config-header" component={TextVariants.h2}>
                            Global Database Configuration
                        </Text>
                    </TextContent>

                    <div className="ds-margin-top-lg">
                        <Tabs isFilled activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                            <Tab eventKey={0} title={<TabTitleText>Limits</TabTitleText>}>
                                <div className="ds-left-indent-md">
                                    <Grid
                                        title="The maximum number of entries that the Directory Server will check when examining candidate entries in response to a search request (nsslapd-lookthrough-limit)."
                                        className="ds-margin-top-xlg"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Database Look Through Limit
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.looklimit}
                                                min={-1}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("looklimit") }}
                                                onChange={(e) => { this.onConfigChange(e, "looklimit", -1, 0) }}
                                                onPlus={() => { this.onPlusConfig("looklimit") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The number of entry IDs that are searched during a search operation (nsslapd-idlistscanlimit)."
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            ID List Scan Limit
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.idscanlimit}
                                                min={100}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("idscanlimit") }}
                                                onChange={(e) => { this.onConfigChange(e, "idscanlimit", 100, 0) }}
                                                onPlus={() => { this.onPlusConfig("idscanlimit") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The maximum number of entries that the Directory Server will check when examining candidate entries for a search which uses the simple paged results control (nsslapd-pagedlookthroughlimit)."
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Paged Search Look Through Limit
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.pagelooklimit}
                                                min={-1}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("pagelooklimit") }}
                                                onChange={(e) => { this.onConfigChange(e, "pagelooklimit", -1, 0) }}
                                                onPlus={() => { this.onPlusConfig("pagelooklimit") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The number of entry IDs that are searched, specifically, for a search operation using the simple paged results control (nsslapd-pagedidlistscanlimit)."
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Paged Search ID List Scan Limit
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.pagescanlimit}
                                                min={-1}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("pagescanlimit") }}
                                                onChange={(e) => { this.onConfigChange(e, "pagescanlimit", -1, 0) }}
                                                onPlus={() => { this.onPlusConfig("pagescanlimit") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The maximum number of entries that the Directory Server will check when examining candidate entries in response to a range search request (nsslapd-rangelookthroughlimit)."
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Range Search Look Through Limit
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.rangelooklimit}
                                                min={-1}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("rangelooklimit") }}
                                                onChange={(e) => { this.onConfigChange(e, "rangelooklimit", -1, 0) }}
                                                onPlus={() => { this.onPlusConfig("rangelooklimit") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                </div>
                            </Tab>

                            <Tab eventKey={1} title={<TabTitleText>Database Cache</TabTitleText>}>
                                <div className="ds-left-indent-md">
                                    <Grid className="ds-margin-top-xlg">
                                        <GridItem span={12}>
                                            <Checkbox
                                                label="Automatic Cache Tuning"
                                                onChange={this.handleChange}
                                                isChecked={db_auto_checked}
                                                aria-label="uncontrolled checkbox example"
                                                id="db_cache_auto"
                                            />
                                        </GridItem>
                                        <GridItem span={12}>
                                            {db_cache_form}
                                        </GridItem>
                                    </Grid>
                                </div>
                            </Tab>

                            <Tab eventKey={2} title={<TabTitleText>Import Cache</TabTitleText>}>
                                <div className="ds-left-indent-md">
                                    <Grid className="ds-margin-top-xlg">
                                        <GridItem span={12}>
                                            <Checkbox
                                                label="Automatic Import Cache Tuning"
                                                title="Set import cache to be set automatically"
                                                onChange={this.handleChange}
                                                isChecked={import_auto_checked}
                                                aria-label="uncontrolled checkbox example"
                                                id="import_cache_auto"
                                            />
                                        </GridItem>
                                        <GridItem span={12}>
                                            {import_cache_form}
                                        </GridItem>
                                    </Grid>
                                </div>
                            </Tab>

                            <Tab eventKey={3} title={<TabTitleText>NDN Cache</TabTitleText>}>
                                <div className="ds-left-indent-md">
                                    <Grid
                                        title="Set the maximum size in bytes for the Normalized DN Cache (nsslapd-ndn-cache-max-size)."
                                        className="ds-margin-top-xlg"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Normalized DN Cache Max Size
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.ndncachemaxsize}
                                                min={1000000}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("ndncachemaxsize") }}
                                                onChange={(e) => { this.onConfigChange(e, "ndncachemaxsize", 1000000, 0) }}
                                                onPlus={() => { this.onPlusConfig("ndncachemaxsize") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                </div>
                            </Tab>

                            <Tab eventKey={4} title={<TabTitleText>Database Locks</TabTitleText>}>
                                <div className="ds-left-indent-md">
                                    <Grid
                                        title="The number of database locks (nsslapd-db-locks)."
                                        className="ds-margin-top-xlg"
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Database Locks
                                        </GridItem>
                                        <GridItem span={10}>
                                            <NumberInput
                                                value={this.state.dblocks}
                                                min={10000}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("dblocks") }}
                                                onChange={(e) => { this.onConfigChange(e, "dblocks", 10000, 0) }}
                                                onPlus={() => { this.onPlusConfig("dblocks") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid className="ds-margin-top-xlg">
                                        <GridItem span={12}>
                                            <div className="ds-inline">
                                                <Checkbox
                                                    label="Enable DB Lock Monitoring"
                                                    id="dblocksMonitoring"
                                                    isChecked={this.state.dblocksMonitoring}
                                                    onChange={this.select_db_locks_monitoring}
                                                    aria-label="uncontrolled checkbox example"
                                                />
                                            </div>
                                            <div className="ds-inline">
                                                <Tooltip
                                                    id="dblockmonitor"
                                                    position="bottom"
                                                    content={
                                                        <div>
                                                            Database lock monitoring checks if the database locks are about
                                                            to be exhausted, and if they are the server will abort all the
                                                            current searches in order to prevent database corruption.
                                                        </div>
                                                    }
                                                >
                                                    <OutlinedQuestionCircleIcon
                                                        className="ds-left-margin"
                                                    />
                                                </Tooltip>
                                            </div>
                                        </GridItem>
                                        <GridItem span={12}>
                                            {dblocksMonitor}
                                        </GridItem>
                                    </Grid>
                                </div>
                            </Tab>

                            <Tab eventKey={5} title={<TabTitleText>Advanced Settings</TabTitleText>}>
                                <div className="ds-left-indent-md">
                                    <Grid
                                        title="Database Transaction Log Location (nsslapd-db-logdirectory)."
                                        className="ds-margin-top-xlg"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Transaction Logs Directory
                                        </GridItem>
                                        <GridItem span={8}>
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
                                        <GridItem className="ds-label" span={4}>
                                            Database Home Directory
                                        </GridItem>
                                        <GridItem span={8}>
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
                                        title="The Time Of Day to perform the database compaction after the compact interval has been met.  Uses the format: 'HH:MM' and defaults to '23:59'. (nsslapd-db-compactdb-time)"
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Database Compaction Time
                                        </GridItem>
                                        <GridItem span={2}>
                                            <TimePicker
                                                time={this.state.compacttime}
                                                onChange={this.handleTimeChange}
                                                is24Hour
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The interval in seconds when the database is compacted (nsslapd-db-compactdb-interval). The default is 30 days at midnight. 0 is no compaction."
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Database Compaction Interval
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.compactinterval}
                                                min={0}
                                                max={this.maxValue}
                                                onMinus={() => { this.onMinusConfig("compactinterval") }}
                                                onChange={(e) => { this.onConfigChange(e, "compactinterval", 0, 0) }}
                                                onPlus={() => { this.onPlusConfig("compactinterval") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="Amount of time in seconds after which the Directory Server sends a checkpoint entry to the database transaction log (nsslapd-db-checkpoint-interval)."
                                        className="ds-margin-top"
                                    >
                                        <GridItem className="ds-label" span={4}>
                                            Database Checkpoint Interval
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                value={this.state.chxpoint}
                                                min={10}
                                                max={300}
                                                onMinus={() => { this.onMinusConfig("chxpoint") }}
                                                onChange={(e) => { this.onConfigChange(e, "chxpoint", 10, 0) }}
                                                onPlus={() => { this.onPlusConfig("chxpoint") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={10}
                                            />
                                        </GridItem>
                                    </Grid>
                                </div>
                            </Tab>
                        </Tabs>
                    </div>

                    <Button
                        className="ds-margin-top-lg"
                        onClick={this.save_db_config}
                        variant="primary"
                        isLoading={this.state.saving}
                        spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.state.saveBtnDisabled || this.state.saving}
                    >
                        {saveBtnName}
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
    data: {},
};
