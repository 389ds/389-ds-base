import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    NumberInput,
    Select,
    SelectOption,
    SelectVariant,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import { PassthroughAuthURLsTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

class PassthroughAuthentication extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            urlRows: [],
            tableKey: 1,
            error: {},
            modalSpinning: false,
            modalChecked: false,
            oldURL: "",
            urlConnType: "ldaps",
            urlAuthDS: "",
            urlSubtree: "",
            urlMaxConns: 3,
            urlMaxOps: 5,
            urlTimeout: 300,
            urlLDVer: "3",
            urlConnLifeTime: 300,
            urlStartTLS: false,
            _urlConnType: "ldaps",
            _urlAuthDS: "",
            _urlSubtree: "",
            _urlMaxConns: 3,
            _urlMaxOps: 5,
            _urlTimeout: 300,
            _urlLDVer: "3",
            _urlConnLifeTime: 300,
            _urlStartTLS: false,
            showConfirmDeleteURL: false,
            saveBtnDisabledPassthru: true,
            savingPassthru: false,

            newURLEntry: false,
            urlEntryModalShow: false
        };

        this.maxValue = 20000000;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validatePassthru() });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            }, () => { this.validatePassthru() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validatePassthru() });
        };

        // This vastly improves rendering performance during handleChange()
        this.attrRows = this.props.attributes.map((attr) => (
            <FormSelectOption key={attr} value={attr} label={attr} />
        ));

        this.handlePassthruChange = this.handlePassthruChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.validatePassthru = this.validatePassthru.bind(this);
        this.loadURLs = this.loadURLs.bind(this);
        this.openURLModal = this.openURLModal.bind(this);
        this.closeURLModal = this.closeURLModal.bind(this);
        this.showEditURLModal = this.showEditURLModal.bind(this);
        this.showAddURLModal = this.showAddURLModal.bind(this);
        this.cmdURLOperation = this.cmdURLOperation.bind(this);
        this.deleteURL = this.deleteURL.bind(this);
        this.addURL = this.addURL.bind(this);
        this.editURL = this.editURL.bind(this);
        this.showConfirmDeleteURL = this.showConfirmDeleteURL.bind(this);
        this.closeConfirmDeleteURL = this.closeConfirmDeleteURL.bind(this);
    }

    componentDidMount() {
        this.loadURLs();
    }

    validatePassthru() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = ['urlAuthDS', 'urlSubtree'];
        const dnAttrs = ['urlSubtree'];

        // Check we have our required attributes set
        for (const attr of reqAttrs) {
            if (this.state[attr] == "") {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        // Check the DN's of our lists
        for (const attr of dnAttrs) {
            if (!valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;

            const configAttrs = [
                'urlSubtree', 'urlConnType', 'urlAuthDS', 'urlMaxConns',
                'urlMaxOps', 'urlTimeout', 'urlLDVer', 'urlConnLifeTime',
                'urlStartTLS'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabledPassthru: !all_good,
            error: errObj
        });
    }

    handlePassthruChange(e) {
        // Pass thru
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validatePassthru() });
    }

    handleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    showConfirmDeleteConfig (name) {
        this.setState({
            showConfirmDeleteConfig: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDeleteConfig () {
        this.setState({
            showConfirmDeleteConfig: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    showConfirmDeleteURL (name) {
        this.setState({
            showConfirmDeleteURL: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDeleteURL () {
        this.setState({
            showConfirmDeleteURL: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    loadURLs() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "ldap-pass-through-auth",
            "list"
        ];
        this.props.toggleLoadingHandler();
        log_cmd("loadURLs", "Get Passthough Authentication Plugin Configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        urlRows: myObject.items,
                        tableKey: tableKey
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadURLs failed", errMsg.desc);
                    }
                    this.props.toggleLoadingHandler();
                });
    }

    showEditURLModal(rowData) {
        this.openURLModal(rowData);
    }

    showAddURLModal() {
        this.openURLModal();
    }

    openURLModal(url) {
        if (!url) {
            this.setState({
                urlEntryModalShow: true,
                newURLEntry: true,
                oldURL: "",
                urlConnType: "ldap",
                urlAuthDS: "",
                urlSubtree: "",
                urlMaxConns: "3",
                urlMaxOps: "5",
                urlTimeout: "300",
                urlLDVer: "3",
                urlConnLifeTime: "300",
                urlStartTLS: false,
                saveBtnDisabledPassthru: true,
            });
        } else {
            const link = url.split(" ")[0];
            const params = url.split(" ")[1];
            this.setState({
                urlEntryModalShow: true,
                oldURL: url,
                newURLEntry: false,
                urlConnType: link.split(":")[0],
                urlAuthDS: link.split("/")[2],
                urlSubtree: link.split("/")[3],
                urlMaxConns: params.split(",")[0],
                urlMaxOps: params.split(",")[1],
                urlTimeout: params.split(",")[2],
                urlLDVer: params.split(",")[3],
                urlConnLifeTime: params.split(",")[4],
                urlStartTLS: !(params.split(",")[5] == "0"),
                saveBtnDisabledPassthru: true,
            });
        }
    }

    closeURLModal() {
        this.setState({
            urlEntryModalShow: false,
            savingPassthru: false
        });
    }

    deleteURL() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "ldap-pass-through-auth",
            "delete",
            this.state.deleteName
        ];

        this.setState({
            modalSpinning: true
        });

        log_cmd("deleteURL", "Delete the Passthough Authentication Plugin URL entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteURL", "Result", content);
                    this.props.addNotification("success", `URL ${this.state.deleteName} was successfully deleted`);
                    this.loadURLs();
                    this.closeConfirmDeleteURL();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the URL removal operation - ${errMsg.desc}`
                    );
                    this.loadURLs();
                    this.closeConfirmDeleteURL();
                });
    }

    addURL() {
        this.cmdURLOperation("add");
    }

    editURL() {
        this.cmdURLOperation("modify");
    }

    cmdURLOperation(action) {
        const {
            oldURL,
            urlConnType,
            urlAuthDS,
            urlSubtree,
            urlMaxConns,
            urlMaxOps,
            urlTimeout,
            urlLDVer,
            urlConnLifeTime,
            urlStartTLS
        } = this.state;

        const constructedURL = `${urlConnType}://${urlAuthDS}/${urlSubtree} ${urlMaxConns},${urlMaxOps},${urlTimeout},${urlLDVer},${urlConnLifeTime},${
            urlStartTLS ? "1" : "0"
        }`;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "ldap-pass-through-auth",
            action
        ];
        if (oldURL != "" && action == "modify") {
            cmd = [...cmd, oldURL, constructedURL];
        } else {
            cmd = [...cmd, constructedURL];
        }

        this.setState({
            savingPassthru: true
        });
        log_cmd(
            "PassthroughAuthOperation",
            `Do the ${action} operation on the Passthough Authentication Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("PassthroughAuthOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${constructedURL}" entry`
                    );
                    this.loadURLs();
                    this.closeURLModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the URL ${action} operation - ${errMsg.desc}`
                    );
                    this.loadURLs();
                    this.closeURLModal();
                });
    }

    render() {
        const {
            urlRows,
            urlConnType,
            urlLDVer,
            urlAuthDS,
            urlSubtree,
            urlMaxConns,
            urlMaxOps,
            urlTimeout,
            urlConnLifeTime,
            urlStartTLS,
            newURLEntry,
            urlEntryModalShow,
            error,
            savingPassthru
        } = this.state;

        const modalURLFields = {
            urlAuthDS: {
                name: "Authentication Hostname",
                id: 'urlAuthDS',
                value: urlAuthDS,
                help: `The authenticating directory host name. The port number of the Directory Server can be given by adding a colon and then the port number. For example, dirserver.example.com:389. If the port number is not specified, the PTA server attempts to connect using either of the standard ports: Port 389 if ldap:// is specified in the URL. Port 636 if ldaps:// is specified in the URL.`
            },
            urlSubtree: {
                name: "Subtree",
                id: 'urlSubtree',
                value: urlSubtree,
                help: `The pass-through subtree. The PTA Directory Server passes through bind requests to the authenticating Directory Server from all clients whose DN is in this subtree.`
            },
        };
        const modalURLNumberFields = {
            urlMaxConns: {
                name: "Maximum Number of Connections",
                value: urlMaxConns,
                id: 'urlMaxConns',
                help: `The maximum number of connections the PTA directory can simultaneously open to the authenticating directory.`
            },
            urlMaxOps: {
                name: "Maximum Number of Simultaneous Operations",
                value: urlMaxOps,
                id: 'urlMaxOps',
                help: `The maximum number of simultaneous operations (usually bind requests) the PTA directory can send to the authenticating directory within a single connection.`
            },
            urlTimeout: {
                name: "Timeout",
                value: urlTimeout,
                id: 'urlTimeout',
                help: `The time limit, in seconds, that the PTA directory waits for a response from the authenticating Directory Server. If this timeout is exceeded, the server returns an error to the client. The default is 300 seconds (five minutes). Specify zero (0) to indicate no time limit should be enforced.`
            },
            urlConnLifeTime: {
                name: "Connection Life Time",
                value: urlConnLifeTime,
                id: 'urlConnLifeTime',
                help: `The time limit, in seconds, within which a connection may be used.`
            }
        };

        let saveBtnName = "Save Config";
        const extraPrimaryProps = {};
        if (this.state.savingPassthru) {
            saveBtnName = "Saving Config ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        const title_url = (newURLEntry ? "Add " : "Edit ") + "Pass-Though Authentication URL";

        return (
            <div className={savingPassthru ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={title_url}
                    isOpen={urlEntryModalShow}
                    onClose={this.closeURLModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newURLEntry ? this.addURL : this.editURL}
                            isDisabled={this.state.saveBtnDisabledPassthru || this.state.savingPassthru}
                            isLoading={this.state.savingPassthru}
                            spinnerAriaValueText={this.state.savingPassthru ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeURLModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top">
                            <GridItem
                                className="ds-label"
                                span={5}
                                title="Defines whether TLS is used for communication between the two Directory Servers."
                            >
                                Connection Type
                            </GridItem>
                            <GridItem span={7}>
                                <FormSelect
                                    id="urlConnType"
                                    value={urlConnType}
                                    onChange={(value, event) => {
                                        this.handlePassthruChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="urlldaps" value="ldaps" label="ldaps" />
                                    <FormSelectOption key="urlldap" value="ldap" label="ldap" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        {Object.entries(modalURLFields).map(([id, content]) => (
                            <Grid key={id}>
                                <GridItem className="ds-label" span={5} title={content.help}>
                                    {content.name}
                                </GridItem>
                                <GridItem span={7}>
                                    <TextInput
                                        value={content.value}
                                        type="text"
                                        id={content.id}
                                        aria-describedby="horizontal-form-name-helper"
                                        name={content.name}
                                        onChange={(str, e) => {
                                            this.handlePassthruChange(e);
                                        }}
                                        validated={error[content.id] ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                </GridItem>
                            </Grid>
                        ))}
                        {Object.entries(modalURLNumberFields).map(([id, content]) => (
                            <Grid key={id}>
                                <GridItem className="ds-label" span={5} title={content.help}>
                                    {content.name}
                                </GridItem>
                                <GridItem span={7}>
                                    <NumberInput
                                        value={content.value}
                                        min={-1}
                                        max={this.maxValue}
                                        onMinus={() => { this.onMinusConfig(content.id) }}
                                        onChange={(e) => { this.onConfigChange(e, content.id, -1) }}
                                        onPlus={() => { this.onPlusConfig(content.id) }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={8}
                                    />
                                </GridItem>
                            </Grid>
                        ))}
                        <Grid>
                            <GridItem
                                className="ds-label"
                                span={5}
                                title="The version of the LDAP protocol used to connect to the authenticating directory. Directory Server supports LDAP version 2 and 3. The default is version 3, and Red Hat strongly recommends against using LDAPv2, which is old and will be deprecated."
                            >
                                Version
                            </GridItem>
                            <GridItem span={7}>
                                <FormSelect
                                    id="urlLDVer"
                                    value={urlLDVer}
                                    onChange={(value, event) => {
                                        this.handlePassthruChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="LDAPv3" value="3" label="LDAPv3" />
                                    <FormSelectOption key="LDAPv2" value="2" label="LDAPv2" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={5}>
                                <Checkbox
                                    id="urlStartTLS"
                                    isChecked={urlStartTLS}
                                    onChange={(checked, e) => { this.handlePassthruChange(e) }}
                                    title="A flag of whether to use Start TLS for the connection to the authenticating directory. Start TLS establishes a secure connection over the standard port, so it is useful for connecting using LDAP instead of LDAPS. The TLS server and CA certificates need to be available on both of the servers. To use Start TLS, the LDAP URL must use ldap:, not ldaps:."
                                    label="Enable StartTLS"
                                />
                            </GridItem>
                        </Grid>
                        <hr />
                        <Grid title="The URL that will be added or modified after you click 'Save'">
                            <GridItem className="ds-label" span={5}>
                                Result URL
                            </GridItem>
                            <GridItem span={7}>
                                <b>
                                    {urlConnType}://{urlAuthDS}/{urlSubtree}{" "}
                                    {urlMaxConns},{urlMaxOps},{urlTimeout},
                                    {urlLDVer},{urlConnLifeTime},
                                    {urlStartTLS ? "1" : "0"}
                                </b>
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Pass Through Authentication"
                    pluginName="Pass Through Authentication"
                    cmdName="ldap-pass-through-auth"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <div className="ds-indent">
                        <PassthroughAuthURLsTable
                            rows={urlRows}
                            key={this.state.tableKey}
                            editConfig={this.showEditURLModal}
                            deleteConfig={this.showConfirmDeleteURL}
                        />
                        <Button
                            variant="primary"
                            onClick={this.showAddURLModal}
                        >
                            Add URL
                        </Button>
                    </div>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteURL}
                    closeHandler={this.closeConfirmDeleteURL}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deleteURL}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Passthru Authentication URL"
                    mMsg="Are you sure you want to delete this URL?"
                    mSpinningMsg="Deleting URL..."
                    mBtnName="Delete URL"
                />
            </div>
        );
    }
}

PassthroughAuthentication.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

PassthroughAuthentication.defaultProps = {
    rows: [],
    serverId: "",
};

export default PassthroughAuthentication;
