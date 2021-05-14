import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Row,
    Spinner,
} from "patternfly-react";
import {
    Button,
    Checkbox
} from "@patternfly/react-core";
import { SASLTable } from "./serverTables.jsx";
import { SASLMappingModal } from "./serverModals.jsx";
import { Typeahead } from "react-bootstrap-typeahead";

export class ServerSASL extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            configLoading: false,
            tableLoading: false,
            loaded: false,
            activeKey: 1,
            errObj: {},
            saveDisabled: true,
            supportedMechs: [],
            mappingKey: 0,

            // Main settings
            allowedMechs: [],
            mappingFallback: false,
            maxBufSize: "",
            // Mapping modal
            showMapping: false,
            saveMappingDisabled: true,
            testRegexDisabled: true,
            testBtnDisabled: true,
            saslMapName: "",
            saslMapRegex: "",
            saslTestText: "",
            saslBase: "",
            saslFilter: "",
            saslPriority: "100",
            saslModalType: "Create",
            saslErrObj: {},
            showConfirmDelete: false,
            modalChecked: false,
        };

        this.handleChange = this.handleChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleModalAddChange = this.handleModalAddChange.bind(this);
        this.handleTestRegex = this.handleTestRegex.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.loadMechs = this.loadMechs.bind(this);
        this.loadSASLMappings = this.loadSASLMappings.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.showCreateMapping = this.showCreateMapping.bind(this);
        this.showEditMapping = this.showEditMapping.bind(this);
        this.closeMapping = this.closeMapping.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.createMapping = this.createMapping.bind(this);
        this.editMapping = this.editMapping.bind(this);
        this.deleteMapping = this.deleteMapping.bind(this);
    }

    componentDidMount() {
        // Loading config
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    handleTestRegex() {
        let test_string = this.state.saslTestText;
        let regex = this.state.saslMapRegex;
        let cleaned_regex = regex.replace(/\\\(/g, '(').replace(/\\\)/g, ')');
        let sasl_regex = RegExp(cleaned_regex);
        if (sasl_regex.test(test_string)) {
            this.props.addNotification(
                "success",
                "The test string matches the Regular Expression"
            );
        } else {
            this.props.addNotification(
                "warning",
                "The test string does not match the Regular Expression"
            );
        }
    }

    handleChange(e) {
        let attr = "";
        let value = "";
        let isArray = false;
        let chkBox = false;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObj;

        // Could be a typeahead change, check if "e" is an Array
        if (Array.isArray(e)) {
            isArray = true;
            attr = "allowedMechs";
            value = e;
        } else {
            value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
            attr = e.target.id;
            if (e.target.type === 'checkbox') {
                chkBox = true;
            }
        }
        // Check if a setting was changed, if so enable the save button
        if (attr == 'mappingFallback' && this.state._mappingFallback != value) {
            disableSaveBtn = false;
        } else if (attr == 'saslPriority' && this.state._saslPriority != value) {
            disableSaveBtn = false;
        } else if (attr == 'maxBufSize' && this.state._maxBufSize != value) {
            disableSaveBtn = false;
        } else if (attr == 'allowedMechs' && this.state._allowedMechs.join(' ') != value.join(' ')) {
            if (this.state._allowedMechs.length > value.length) {
                // The way allow mechanisms work if that once you set it initially
                // you can't edit it without removing all the current mecahisms.  So
                // if we remove one, just remove them all and make the user start over.
                // !! THIS DOES NOT WORK
                value = [];
            }
            disableSaveBtn = false;
        }

        // Now check for differences in values that we did not touch
        if (attr != 'mappingFallback' && this.state._mappingFallback != this.state.mappingFallback) {
            disableSaveBtn = false;
        } else if (attr != 'saslPriority' && this.state._saslPriority != this.state.saslPriority) {
            disableSaveBtn = false;
        } else if (attr != 'maxBufSize' && this.state._maxBufSize != this.state.maxBufSize) {
            disableSaveBtn = false;
        } else if (attr != 'allowedMechs' && this.state._allowedMechs.join(' ') != this.state.allowedMechs.join(' ')) {
            disableSaveBtn = false;
        }

        if (!isArray && !chkBox && value == "") {
            valueErr = true;
            disableSaveBtn = true;
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            saveDisabled: disableSaveBtn,
            errObj: errObj,
        });
    }

    handleModalAddChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let disableRegexTestBtn = true;
        let valueErr = false;
        let errObj = this.state.errObj;

        // Check if a setting was changed, if so enable the save button
        if (attr == 'saslMapName' && value != "") {
            disableSaveBtn = false;
        } else if (attr == 'saslMapRegex' && value != "") {
            disableSaveBtn = false;
        } else if (attr == 'saslBase' && value != "") {
            disableSaveBtn = false;
        } else if (attr == 'saslPriority' && value != "0") {
            disableSaveBtn = false;
        } else if (attr == 'saslFilter' && value != "") {
            disableSaveBtn = false;
        }
        if (!disableSaveBtn) {
            // Make sure every other field is set
            if (attr != 'saslMapName' && this.state.saslMapName == "") {
                disableSaveBtn = true;
            }
            if (attr != 'saslMapRegex' && this.state.saslMapRegex == "") {
                disableSaveBtn = true;
            }
            if (attr != 'saslBase' && this.state.saslBase == "") {
                disableSaveBtn = true;
            }
            if (attr != 'saslFilter' && this.state.saslFilter == "") {
                disableSaveBtn = true;
            }
        }

        // Handle Test Text field and buttons
        if (attr == 'saslTestText' && value != "" && this.state.saslMapRegex != "") {
            disableRegexTestBtn = false;
        }
        if (attr != 'saslTestText' && this.state.saslMapRegex != "" && this.state.saslTestText != "") {
            disableRegexTestBtn = false;
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            saveMappingDisabled: disableSaveBtn,
            testBtnDisabled: disableRegexTestBtn,
            errObj: errObj,
        });
    }

    handleModalChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let disableRegexTestBtn = true;
        let valueErr = false;
        let errObj = this.state.errObj;

        // Check if a setting was changed, if so enable the save button
        if (attr == 'saslMapName' && this.state._saslMapName != value) {
            disableSaveBtn = false;
        } else if (attr == 'saslMapRegex' && this.state._saslMapRegex != value) {
            disableSaveBtn = false;
        } else if (attr == 'saslBase' && this.state._saslBase != value) {
            disableSaveBtn = false;
        } else if (attr == 'saslFilter' && this.state._saslFilter != value) {
            disableSaveBtn = false;
        } else if (attr == 'saslPriority' && this.state._saslPriority != value) {
            disableSaveBtn = false;
        }

        // Now check for differences in values that we did not touch
        if (attr != 'saslMapName' && this.state._saslMapName != this.state.saslMapName) {
            disableSaveBtn = false;
        } else if (attr != 'saslMapRegex' && this.state._saslMapRegex != this.state.saslMapRegex) {
            disableSaveBtn = false;
        } else if (attr != 'saslBase' && this.state._saslBase != this.state.saslBase) {
            disableSaveBtn = false;
        } else if (attr != 'saslFilter' && this.state._saslFilter != this.state.saslFilter) {
            disableSaveBtn = false;
        } else if (attr != 'saslPriority' && this.state._saslPriority != this.state.saslPriority) {
            disableSaveBtn = false;
        }

        // Handle TEst Text filed and buttons
        if (attr == 'saslTestText' && value != "" && this.state.saslMapRegex != "") {
            disableRegexTestBtn = false;
        }
        if (attr != 'saslTestText' && this.state.saslMapRegex != "" && this.state.saslTestText != "") {
            disableRegexTestBtn = false;
        }

        if (value == "" && attr != "saslTestText") {
            valueErr = true;
            disableSaveBtn = true;
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            saveMappingDisabled: disableSaveBtn,
            testBtnDisabled: disableRegexTestBtn,
            errObj: errObj,
        });
    }

    loadConfig() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", 'get'
        ];
        log_cmd("loadConfig", "Get SASL settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let allowedMechsVal = attrs['nsslapd-allowed-sasl-mechanisms'][0];
                    let allowedMechs = [];
                    let fallback = false;

                    if (attrs['nsslapd-sasl-mapping-fallback'][0] == "on") {
                        fallback = true;
                    }
                    if (allowedMechsVal != "") {
                        // Could be space or comma separated
                        if (allowedMechsVal.indexOf(',') > -1) {
                            allowedMechsVal = allowedMechsVal.trim();
                            allowedMechs = allowedMechsVal.split(',');
                        } else {
                            allowedMechs = allowedMechsVal.split();
                        }
                    }

                    this.setState({
                        maxBufSize: attrs['nsslapd-sasl-max-buffer-size'][0],
                        allowedMechs: allowedMechs,
                        mappingFallback: fallback,
                        saveDisabled: true,
                        // Store original values
                        _maxBufSize: attrs['nsslapd-sasl-max-buffer-size'][0],
                        _allowedMechs: allowedMechs,
                        _mappingFallback: fallback,
                    }, this.loadMechs());
                });
    }

    loadMechs() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket", "sasl", 'get-mechs'
        ];
        log_cmd("loadMechs", "Get supported SASL mechanisms", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        supportedMechs: config.items
                    }, this.loadSASLMappings());
                });
    }

    loadSASLMappings() {
        let cmd = ["dsconf", '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket", 'sasl', 'list', '--details'];
        log_cmd('get_and_set_sasl', 'Get SASL mappings', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let saslMapObj = JSON.parse(content);
                    let mappings = [];
                    for (let mapping of saslMapObj['items']) {
                        if (!mapping['attrs'].hasOwnProperty('nssaslmappriority')) {
                            mapping['attrs'].nssaslmappriority = ['100'];
                        }
                        mappings.push(mapping['attrs']);
                    }
                    let key = this.state.mappingKey + 1;
                    this.setState({
                        mappings: mappings,
                        mappingKey: key,
                        loaded: true,
                        tableLoading: false,
                        configLoading: false,
                        showMappingModal: false,
                        showConfirmDelete: false,
                    }, this.props.enableTree);
                });
    }

    showCreateMapping() {
        this.setState({
            showMappingModal: true,
            saveMappingDisabled: true,
            testRegexDisabled: true,
            saslModalType: "Create",
            saslMapName: "",
            saslMapRegex: "",
            saslTestText: "",
            saslBase: "",
            saslFilter: "",
            saslPriority: "100",
            saslErrObj: {},
        });
    }

    closeMapping() {
        this.setState({
            showMappingModal: false,
        });
    }

    showEditMapping(name, regex, base, filter, priority) {
        this.setState({
            showMappingModal: true,
            saveMappingDisabled: true,
            testRegexDisabled: true,
            saslModalType: "Edit",
            saslMapName: name,
            saslMapRegex: regex,
            saslTestText: "",
            saslBase: base,
            saslFilter: filter,
            saslPriority: priority,
            // Note original values
            _saslMapName: name,
            _saslMapRegex: regex,
            _saslTestText: "",
            _saslBase: base,
            _saslFilter: filter,
            _saslPriority: priority,
            saslErrObj: {},
        });
    }

    showConfirmDelete(name) {
        this.setState({
            saslMapName: name,
            modalChecked: false,
            showConfirmDelete: true,
        });
    }

    closeConfirmDelete() {
        this.setState({
            modalChecked: false,
            showConfirmDelete: false,
        });
    }

    createMapping() {
        this.setState({
            tableLoading: true,
        });
        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'sasl', 'create',
            '--cn=' + this.state.saslMapName,
            '--nsSaslMapFilterTemplate=' + this.state.saslFilter,
            '--nsSaslMapRegexString=' + this.state.saslMapRegex,
            '--nsSaslMapBaseDNTemplate=' + this.state.saslBase,
            '--nsSaslMapPriority=' + this.state.saslPriority
        ];

        log_cmd("createMapping", "Create sasl mapping", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully create new SASL Mapping"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig();
                    this.props.addNotification(
                        "error",
                        `Error creating new SASL Mapping - ${errMsg.desc}`
                    );
                });
    }

    editMapping(name) {
        // Start spinning
        let new_mappings = this.state.mappings;
        for (let saslMap of new_mappings) {
            if (saslMap.cn[0] == name) {
                saslMap.nssaslmapregexstring = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmapregexstring[0]} loading size="sm" />];
                saslMap.nssaslmapbasedntemplate = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmapbasedntemplate[0]} loading size="sm" />];
                saslMap.nssaslmapfiltertemplate = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmapfiltertemplate[0]} loading size="sm" />];
                saslMap.nssaslmappriority = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmappriority[0]} loading size="sm" />];
            }
        }

        this.setState({
            mappings: new_mappings,
            tableLoading: true
        });

        // Delete and create
        let delete_cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'sasl', 'delete', this.state._saslMapName
        ];
        let create_cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'sasl', 'create',
            '--cn=' + this.state.saslMapName,
            '--nsSaslMapFilterTemplate=' + this.state.saslFilter,
            '--nsSaslMapRegexString=' + this.state.saslMapRegex,
            '--nsSaslMapBaseDNTemplate=' + this.state.saslBase,
            '--nsSaslMapPriority=' + this.state.saslPriority
        ];

        log_cmd("editMapping", "deleting sasl mapping", delete_cmd);
        cockpit
                .spawn(delete_cmd, {superuser: true, "err": "message"})
                .done(content => {
                    log_cmd("editMapping", "Create new sasl mapping", create_cmd);
                    cockpit
                            .spawn(create_cmd, {superuser: true, "err": "message"})
                            .done(content => {
                                this.loadConfig();
                                this.props.addNotification(
                                    "success",
                                    "Successfully updated SASL Mapping"
                                );
                            })
                            .fail(err => {
                                let errMsg = JSON.parse(err);
                                this.closeMapping();
                                this.loadConfig();
                                this.props.addNotification(
                                    "error",
                                    `Error updating SASL Mapping - ${errMsg.desc}`
                                );
                            });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig();
                    this.closeMapping();
                    this.props.addNotification(
                        "error",
                        `Error replacing SASL Mapping - ${errMsg.desc}`
                    );
                });
    }

    deleteMapping() {
        // Start spinning
        let new_mappings = this.state.mappings;
        for (let saslMap of new_mappings) {
            if (saslMap.cn[0] == this.state.saslMapName) {
                saslMap.nssaslmapregexstring = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmapregexstring[0]} loading size="sm" />];
                saslMap.nssaslmapbasedntemplate = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmapbasedntemplate[0]} loading size="sm" />];
                saslMap.nssaslmapfiltertemplate = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmapfiltertemplate[0]} loading size="sm" />];
                saslMap.nssaslmappriority = [<Spinner className="ds-lower-field" key={new_mappings[0].nssaslmappriority[0]} loading size="sm" />];
            }
        }
        this.setState({
            mappings: new_mappings,
            tableLoading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'sasl', 'delete', this.state.saslMapName
        ];
        log_cmd("deleteMapping", "Delete sasl mapping", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully deleted SASL Mapping"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig();
                    this.closeConfirmDelete();
                    this.props.addNotification(
                        "error",
                        `Error deleting SASL Mapping - ${errMsg.desc}`
                    );
                });
    }

    saveConfig() {
        // Start spinning
        this.setState({
            configLoading: true,
        });

        // Build up the command list
        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket", 'config', 'replace'
        ];

        let mech_str_new = this.state.allowedMechs.join(' ');
        let mech_str_orig = this.state._allowedMechs.join(' ');
        if (mech_str_orig != mech_str_new) {
            cmd.push("nsslapd-allowed-sasl-mechanisms=" + mech_str_new);
        }
        if (this.state._mappingFallback != this.state.mappingFallback) {
            let value = "off";
            if (this.state.mappingFallback) {
                value = "on";
            }
            cmd.push("nsslapd-sasl-mapping-fallback=" + value);
        }
        if (this.state._maxBufSize != this.state.maxBufSize) {
            cmd.push("nsslapd-sasl-max-buffer-size=" + this.state.maxBufSize);
        }

        log_cmd("saveConfig", "Applying SASL config change", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully updated SASL configuration.  These " +
                            "changes require the server to be restarted to take effect."
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig();
                    this.props.addNotification(
                        "error",
                        `Error updating SASL configuration - ${errMsg.desc}`
                    );
                });
    }

    render() {
        let configSpinner = "";
        let tableSpinner = " ";
        let body = "";
        if (this.state.tableLoading) {
            tableSpinner = <Spinner loading size="sm" />;
        }
        if (this.state.configLoading) {
            configSpinner = <Spinner loading size="md" />;
        }

        if (!this.state.loaded) {
            body =
                <div className="ds-loading-spinner ds-margin-top ds-center">
                    <h4>Loading SASL configuration ...</h4>
                    <Spinner className="ds-margin-top" loading size="md" />
                </div>;
        } else {
            body =
                <div className="ds-margin-left-sm">
                    <Row>
                        <Col sm={3} className="ds-word-wrap">
                            <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                                SASL Settings
                                <Icon className="ds-left-margin ds-refresh"
                                    type="fa" name="refresh" title="Refresh SASL configuration"
                                    onClick={() => {
                                        this.loadConfig();
                                    }}
                                />
                            </ControlLabel>
                        </Col>
                        <Col sm={1} className="ds-margin-top-lg">
                            {configSpinner}
                        </Col>
                    </Row>
                    <hr />
                    <Form>
                        <Row title="The maximum SASL buffer size in bytes (nsslapd-sasl-max-buffer-size)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={3}>
                                Max SASL Buffer Size
                            </Col>
                            <Col sm={4}>
                                <FormControl
                                    id="maxBufSize"
                                    type="number"
                                    min="-1"
                                    max="2147483647"
                                    value={this.state.maxBufSize}
                                    onChange={this.handleChange}
                                />
                            </Col>
                        </Row>
                        <Row
                            title="A list of SASL mechanisms the server will only accept (nsslapd-allowed-sasl-mechanisms).  The default is all mechanisms are allowed."
                            className="ds-margin-top"
                        >
                            <Col componentClass={ControlLabel} sm={3}>
                                Allowed SASL Mechanisms
                            </Col>
                            <Col sm={4}>
                                <Typeahead
                                    id="allowedMechs"
                                    onChange={value => {
                                        this.handleChange(value);
                                    }}
                                    multiple
                                    options={this.state.supportedMechs}
                                    selected={this.state.allowedMechs}
                                    placeholder="Type SASL mechanism to allow"
                                    ref={(typeahead) => { this.typeahead = typeahead }}
                                />
                            </Col>
                        </Row>
                        <Row
                            title="Check all sasl mappings until one succeeds or they all fail (nsslapd-sasl-mapping-fallback)."
                            className="ds-margin-top"
                        >
                            <Checkbox
                                isChecked={this.state.mappingFallback}
                                id="mappingFallback"
                                onChange={(checked, e) => {
                                    this.handleChange(e);
                                }}
                                label="Allow SASL Mapping Fallback"
                            />
                        </Row>
                    </Form>
                    <Button
                        isDisabled={this.state.saveDisabled}
                        variant="primary"
                        className="ds-margin-top-med"
                        onClick={this.saveConfig}
                    >
                        Save Settings
                    </Button>
                    <hr />
                    <Row>
                        <h4 className="ds-center">
                            <div className="ds-inline">
                                <ControlLabel>
                                    <b>SASL Mappings</b>
                                </ControlLabel>
                            </div>
                            <div className="ds-left-indent ds-inline">
                                <ControlLabel>
                                    {tableSpinner}
                                </ControlLabel>
                            </div>
                        </h4>
                    </Row>
                    <SASLTable
                        key={this.state.mappingKey}
                        rows={this.state.mappings}
                        editMapping={this.showEditMapping}
                        deleteMapping={this.showConfirmDelete}
                        className="ds-margin-top"
                    />
                    <Button
                        variant="primary"
                        className="ds-margin-top-med"
                        onClick={this.showCreateMapping}
                    >
                        Create New Mapping
                    </Button>
                </div>;
        }

        return (
            <div id="server-sasl-page">
                {body}
                <SASLMappingModal
                    showModal={this.state.showMappingModal}
                    testBtnDisabled={this.state.testBtnDisabled}
                    saveDisabled={this.state.saveMappingDisabled}
                    closeHandler={this.closeMapping}
                    handleChange={this.state.saslModalType == "Create" ? this.handleModalAddChange : this.handleModalChange}
                    handleTestRegex={this.handleTestRegex}
                    saveHandler={this.state.saslModalType == "Create" ? this.createMapping : this.editMapping}
                    error={this.state.saslErrObj}
                    type={this.state.saslModalType}
                    name={this.state.saslMapName}
                    regex={this.state.saslMapRegex}
                    testText={this.state.saslTestText}
                    base={this.state.saslBase}
                    filter={this.state.saslFilter}
                    priority={this.state.saslPriority}
                    spinning={this.state.tableLoading}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deleteMapping}
                    item={this.state.saslMapName}
                    checked={this.state.modalChecked}
                    spinning={this.state.tableLoading}
                    mTitle="Delete SASL Mapping"
                    mMsg="Are you sure you want to delete this SASL mapping?"
                    mSpinningMsg="Deleting SASL Mapping ..."
                    mBtnName="Delete Mapping"
                />
            </div>
        );
    }
}
