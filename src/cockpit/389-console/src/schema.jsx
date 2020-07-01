import cockpit from "cockpit";
import React from "react";
import { log_cmd, searchFilter } from "./lib/tools.jsx";
import {
    ObjectClassesTable,
    AttributesTable,
    MatchingRulesTable
} from "./lib/schema/schemaTables.jsx";
import { ObjectClassModal, AttributeTypeModal } from "./lib/schema/schemaModals.jsx";
import { DoubleConfirmModal } from "./lib/notifications.jsx";
import {
    Nav,
    NavItem,
    Checkbox,
    TabContainer,
    TabContent,
    TabPane,
    Spinner,
    noop,
    Button
} from "patternfly-react";
import PropTypes from "prop-types";

export class Schema extends React.Component {
    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(4)) {
            if (this.state.firstLoad) {
                this.loadSyntaxesFirst();
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.loadSyntaxesFirst();
                }
            }
        }
    }

    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            loading: false,
            activeKey: 1,

            objectclassRows: [],
            filteredObjectclassRows: [],
            attributesRows: [],
            filteredAttributesRows: [],
            matchingrulesRows: [],
            syntaxes: [],
            attributes: [],
            objectclasses: [],
            matchingrules: [],
            deleteName: "",

            ocModalViewOnly: false,
            ocName: "",
            ocDesc: "",
            ocOID: "",
            ocParent: [],
            ocKind: "",
            ocMust: [],
            ocMay: [],
            ocUserDefined: false,
            objectclassModalShow: false,
            newOcEntry: true,
            ocTableLoading: false,
            ocModalLoading: false,

            atName: "",
            atDesc: "",
            atOID: "",
            atParent: [],
            atSyntax: [],
            atUsage: "userApplications",
            atMultivalued: false,
            atNoUserMod: false,
            atAlias: [],
            atEqMr: [],
            atOrder: [],
            atSubMr: [],
            atUserDefined: false,
            atModalViewOnly: false,
            attributeModalShow: false,
            newAtEntry: true,
            atTableLoading: false,
            atModalLoading: false
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.loadSchemaData = this.loadSchemaData.bind(this);
        this.loadSyntaxesFirst = this.loadSyntaxesFirst.bind(this);
        this.toggleLoading = this.toggleLoading.bind(this);

        this.showViewObjectclassModal = this.showViewObjectclassModal.bind(this);
        this.showEditObjectclassModal = this.showEditObjectclassModal.bind(this);
        this.showAddObjectclassModal = this.showAddObjectclassModal.bind(this);
        this.openObjectclassModal = this.openObjectclassModal.bind(this);
        this.closeObjectclassModal = this.closeObjectclassModal.bind(this);
        this.doDeleteOC = this.doDeleteOC.bind(this);
        this.addObjectclass = this.addObjectclass.bind(this);
        this.editObjectclass = this.editObjectclass.bind(this);
        this.cmdOperationObjectclass = this.cmdOperationObjectclass.bind(this);
        this.showConfirmOCDelete = this.showConfirmOCDelete.bind(this);
        this.closeConfirmOCDelete = this.closeConfirmOCDelete.bind(this);

        this.showViewAttributeModal = this.showViewAttributeModal.bind(this);
        this.showEditAttributeModal = this.showEditAttributeModal.bind(this);
        this.showAddAttributeModal = this.showAddAttributeModal.bind(this);
        this.openAttributeModal = this.openAttributeModal.bind(this);
        this.closeAttributeModal = this.closeAttributeModal.bind(this);
        this.doDeleteAttr = this.doDeleteAttr.bind(this);
        this.addAttribute = this.addAttribute.bind(this);
        this.editAttribute = this.editAttribute.bind(this);
        this.cmdOperationAttribute = this.cmdOperationAttribute.bind(this);
        this.showConfirmAttrDelete = this.showConfirmAttrDelete.bind(this);
        this.closeConfirmAttrDelete = this.closeConfirmAttrDelete.bind(this);
    }

    toggleLoading(item) {
        if (item == "allSchema") {
            this.setState(prevState => ({
                loading: !prevState.loading
            }));
        } else if (item == "ocTable") {
            this.setState(prevState => ({
                ocTableLoading: !prevState.ocTableLoading
            }));
        } else if (item == "ocModal") {
            this.setState(prevState => ({
                ocModalLoading: !prevState.ocModalLoading
            }));
        } else if (item == "atTable") {
            this.setState(prevState => ({
                atTableLoading: !prevState.atTableLoading
            }));
        } else if (item == "atModal") {
            this.setState(prevState => ({
                atModalLoading: !prevState.atModalLoading
            }));
        }
    }

    loadSyntaxesFirst() {
        if (this.state.firstLoad) {
            this.setState({
                firstLoad: false
            });
        }
        this.toggleLoading("allSchema");
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "get_syntaxes"
        ];
        log_cmd("loadSyntaxes", "Get syntaxes for attributetypes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        syntaxes: myObject.items
                    });
                    this.loadSchemaData(true);
                })
                .fail(err => {
                    if (err != 0) {
                        let errMsg = JSON.parse(err);
                        console.log("loadSyntaxes failed: ", errMsg.desc);
                    }
                });
    }

    loadSchemaData(initialLoading) {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "list"
        ];
        log_cmd("loadSchemaData", "Get schema objects in one batch", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    let attrs = [];
                    let ocs = [];
                    let mrs = [];
                    for (let content of myObject.attributetypes.items) {
                        attrs.push({
                            id: content.name[0],
                            label: content.name[0]
                        });
                    }
                    for (let content of myObject.objectclasses.items) {
                        ocs.push({
                            id: content.name[0],
                            label: content.name[0]
                        });
                    }
                    for (let content of myObject.matchingrules.items) {
                        mrs.push({
                            id: content.name[0],
                            label: content.name[0]
                        });
                    }
                    this.setState({
                        objectclassRows: myObject.objectclasses.items,
                        attributesRows: myObject.attributetypes.items,
                        matchingrulesRows: myObject.matchingrules.items,
                        attributes: attrs,
                        matchingrules: mrs,
                        objectclasses: ocs
                    });
                    if (this.state.ocUserDefined) {
                        this.setState({
                            filteredObjectclassRows: searchFilter(
                                "user defined",
                                ["x_origin"],
                                myObject.objectclasses.items
                            )
                        });
                    } else {
                        this.setState({
                            filteredObjectclassRows: myObject.objectclasses.items
                        });
                    }
                    if (this.state.atUserDefined) {
                        this.setState({
                            filteredAttributesRows: searchFilter(
                                "user defined",
                                ["x_origin"],
                                myObject.attributetypes.items
                            )
                        });
                    } else {
                        this.setState({
                            filteredAttributesRows: myObject.attributetypes.items
                        });
                    }
                    if (initialLoading) {
                        this.toggleLoading("allSchema");
                    }
                })
                .fail(err => {
                    if (err != 0) {
                        let errMsg = JSON.parse(err);
                        console.log("loadSchemaData failed: ", errMsg.desc);
                    }
                    if (initialLoading) {
                        this.toggleLoading("allSchema");
                    }
                });
    }

    showViewObjectclassModal(rowData) {
        this.setState({
            ocModalViewOnly: true
        });
        this.openObjectclassModal(rowData.name[0]);
    }

    showEditObjectclassModal(rowData) {
        this.setState({
            ocModalViewOnly: false
        });
        this.openObjectclassModal(rowData.name[0]);
    }

    showAddObjectclassModal(rowData) {
        this.setState({
            ocModalViewOnly: false
        });
        this.openObjectclassModal();
    }

    openObjectclassModal(name) {
        if (!name) {
            this.setState({
                ocName: "",
                ocDesc: "",
                ocOID: "",
                ocParent: [],
                ocKind: "",
                ocMust: [],
                ocMay: [],
                objectclassModalShow: true,
                newOcEntry: true
            });
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "schema",
                "objectclasses",
                "query",
                name
            ];

            this.toggleLoading("ocTable");
            log_cmd("openObjectclassModal", "Fetch ObjectClass data from schema", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let obj = JSON.parse(content);
                        let item = obj.oc;
                        let ocMustList = [];
                        let ocMayList = [];
                        const kindOpts = ["STRUCTURAL", "ABSTRACT", "AUXILIARY"];
                        this.setState({
                            objectclassModalShow: true,
                            newOcEntry: false,
                            ocName: item["name"] === undefined ? "" : item["name"][0],
                            ocDesc: item["desc"] === null ? "" : item["desc"][0],
                            ocOID: item["oid"] === undefined ? "" : item["oid"][0],
                            ocKind: item["kind"] === undefined ? "" : kindOpts[item["kind"]],
                            ocParent:
                            item["sup"].length == 0
                                ? []
                                : [
                                    {
                                        id: item["sup"][0],
                                        label: item["sup"][0]
                                    }
                                ]
                        });
                        if (item["must"] === undefined) {
                            this.setState({ ocMust: [] });
                        } else {
                            for (let value of item["must"]) {
                                ocMustList = [...ocMustList, { id: value, label: value }];
                            }
                            this.setState({
                                ocMust: ocMustList
                            });
                        }
                        if (item["may"] === undefined) {
                            this.setState({ ocMay: [] });
                        } else {
                            for (let value of item["may"]) {
                                ocMayList = [...ocMayList, { id: value, label: value }];
                            }
                            this.setState({
                                ocMay: ocMayList
                            });
                        }
                        this.toggleLoading("ocTable");
                    })
                    .fail(_ => {
                        this.setState({
                            ocName: "",
                            ocDesc: "",
                            ocOID: "",
                            ocParent: [],
                            ocKind: "",
                            ocMust: [],
                            ocMay: [],
                            objectclassModalShow: true,
                            newOcEntry: true
                        });
                        this.toggleLoading("ocTable");
                    });
        }
    }

    closeObjectclassModal() {
        this.setState({ objectclassModalShow: false });
    }

    closeConfirmOCDelete () {
        // call doDeleteOC
        this.setState({
            showConfirmDeleteOC: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    showConfirmOCDelete(oc_name) {
        this.setState({
            showConfirmDeleteOC: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: oc_name
        });
    }

    doDeleteOC() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "objectclasses",
            "remove",
            this.state.deleteName
        ];

        this.setState({
            modalSpinning: true,
        });

        log_cmd("deleteObjectclass", "Delete ObjectClass from schema", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteObjectclass", "Result", content);
                    this.props.addNotification("success", `ObjectClass ${this.state.deleteName} was successfully deleted`);
                    this.loadSchemaData();
                    this.closeConfirmOCDelete();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during ObjectClass removal operation - ${errMsg.desc}`
                    );
                    this.loadSchemaData();
                    this.closeConfirmOCDelete();
                });
    }

    addObjectclass() {
        this.cmdOperationObjectclass("add");
    }

    editObjectclass() {
        this.cmdOperationObjectclass("replace");
    }

    cmdOperationObjectclass(action) {
        const { ocName, ocDesc, ocOID, ocParent, ocKind, ocMust, ocMay } = this.state;
        if (ocName == "") {
            this.props.addNotification("warning", "ObjectClass Name is required.");
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "schema",
                "objectclasses",
                action,
                ocName
            ];
            // Process and validate parameters
            if (ocOID != "") {
                cmd = [...cmd, "--oid", ocOID];
            }
            if (ocParent.length != 0) {
                cmd = [...cmd, "--sup", ocParent[0].label];
            }
            if (ocKind != "") {
                cmd = [...cmd, "--kind", ocKind];
            }
            if (ocDesc != "") {
                cmd = [...cmd, "--desc", ocDesc];
            }
            if (ocMust.length != 0) {
                cmd = [...cmd, "--must"];
                for (let value of ocMust) {
                    cmd = [...cmd, value.label];
                }
            }
            if (ocMay.length != 0) {
                cmd = [...cmd, "--may"];
                for (let value of ocMay) {
                    cmd = [...cmd, value.label];
                }
            }

            this.toggleLoading("ocModal");
            log_cmd("cmdOperationObjectclass", `Do the ${action} operation on ObjectClass`, cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info("cmdOperationObjectclass", "Result", content);
                        this.props.addNotification(
                            "success",
                            `ObjectClass ${ocName} - ${action} operation was successfull`
                        );
                        this.loadSchemaData();
                        this.closeObjectclassModal();
                        this.toggleLoading("ocModal");
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the ObjectClass ${action} operation - ${errMsg.desc}`
                        );
                        this.loadSchemaData();
                        this.closeObjectclassModal();
                        this.toggleLoading("ocModal");
                    });
        }
    }

    showViewAttributeModal(rowData) {
        this.setState({
            atModalViewOnly: true
        });
        this.openAttributeModal(rowData.name[0]);
    }

    showEditAttributeModal(rowData) {
        this.setState({
            atModalViewOnly: false
        });
        this.openAttributeModal(rowData.name[0]);
    }

    showAddAttributeModal(rowData) {
        this.setState({
            atModalViewOnly: false
        });
        this.openAttributeModal();
    }

    openAttributeModal(name) {
        if (!name) {
            this.setState({
                atName: "",
                atDesc: "",
                atOID: "",
                atParent: [],
                atSyntax: [],
                atUsage: "userApplications",
                atMultivalued: false,
                atNoUserMod: false,
                atAlias: [],
                atEqMr: [],
                atOrder: [],
                atSubMr: [],
                attributeModalShow: true,
                newAtEntry: true
            });
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "schema",
                "attributetypes",
                "query",
                name
            ];

            this.toggleLoading("atTable");
            log_cmd("openAttributeModal", "Fetch Attribute data from schema", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let obj = JSON.parse(content);
                        let item = obj.at;
                        let atAliasList = [];
                        const atUsageOpts = [
                            "userApplications",
                            "directoryOperation",
                            "distributedOperation",
                            "dSAOperation"
                        ];
                        this.setState({
                            attributeModalShow: true,
                            newAtEntry: false,
                            atName: item["name"] === undefined ? "" : item["name"][0],
                            atDesc: item["desc"] === null ? "" : item["desc"][0],
                            atOID: item["oid"] === undefined ? "" : item["oid"][0],
                            atParent:
                            item["sup"].length == 0
                                ? []
                                : [
                                    {
                                        id: item["sup"][0],
                                        label: item["sup"][0]
                                    }
                                ],
                            atSyntax:
                            item["syntax"] === undefined
                                ? []
                                : [
                                    {
                                        id: item["syntax"][0],
                                        label: this.state.syntaxes.filter(
                                            attr => attr.id === item["syntax"][0]
                                        )[0]["label"]
                                    }
                                ],
                            atUsage: item["usage"] === undefined ? "" : atUsageOpts[item["usage"]],
                            atMultivalued: !item["single_value"],
                            atNoUserMod: item["no_user_mod"],
                            atEqMr:
                            item["equality"] === null
                                ? []
                                : [
                                    {
                                        id: item["equality"][0],
                                        label: item["equality"][0]
                                    }
                                ],
                            atOrder:
                            item["ordering"] === null
                                ? []
                                : [
                                    {
                                        id: item["ordering"][0],
                                        label: item["ordering"][0]
                                    }
                                ],
                            atSubMr:
                            item["substr"] === null
                                ? []
                                : [
                                    {
                                        id: item["substr"][0],
                                        label: item["substr"][0]
                                    }
                                ]
                        });
                        if (item["aliases"] === null) {
                            this.setState({ atAlias: [] });
                        } else {
                            for (let value of item["aliases"]) {
                                atAliasList = [...atAliasList, { id: value, label: value }];
                            }
                            this.setState({
                                atAlias: atAliasList
                            });
                        }
                        this.toggleLoading("atTable");
                    })
                    .fail(_ => {
                        this.setState({
                            atName: "",
                            atDesc: "",
                            atOID: "",
                            atParent: [],
                            atSyntax: [],
                            atUsage: "userApplications",
                            atMultivalued: false,
                            atNoUserMod: false,
                            atAlias: [],
                            atEqMr: [],
                            atOrder: [],
                            atSubMr: [],
                            attributeModalShow: true,
                            newAtEntry: true
                        });
                        this.toggleLoading("atTable");
                    });
        }
    }

    closeAttributeModal() {
        this.setState({
            attributeModalShow: false
        });
    }

    closeConfirmAttrDelete () {
        this.setState({
            showConfirmAttrDelete: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    showConfirmAttrDelete(attr_name) {
        this.setState({
            showConfirmAttrDelete: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: attr_name
        });
    }

    doDeleteAttr() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "remove",
            this.state.deleteName
        ];

        this.setState({
            modalSpinning: true,
        });

        log_cmd("deleteAttribute", "Delete Attribute from schema", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteAttribute", "Result", content);
                    this.props.addNotification("success", `Attribute ${this.state.deleteName} was successfully deleted`);
                    this.loadSchemaData();
                    this.closeConfirmAttrDelete();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during Attribute removal operation - ${errMsg.desc}`
                    );
                    this.loadSchemaData();
                    this.closeConfirmAttrDelete();
                });
    }

    addAttribute() {
        this.cmdOperationAttribute("add");
    }

    editAttribute() {
        this.cmdOperationAttribute("replace");
    }

    cmdOperationAttribute(action) {
        const {
            atName,
            atDesc,
            atOID,
            atParent,
            atSyntax,
            atUsage,
            atMultivalued,
            atNoUserMod,
            atAlias,
            atEqMr,
            atOrder,
            atSubMr
        } = this.state;

        if (atName == "" || atSyntax.length == 0) {
            this.props.addNotification("warning", "Attribute Name and Syntax are required.");
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "schema",
                "attributetypes",
                action,
                atName
            ];
            cmd = [...cmd, "--syntax", atSyntax[0].id];
            if (atAlias.length != 0) {
                cmd = [...cmd, "--aliases"];
                for (let value of atAlias) {
                    cmd = [...cmd, value.label];
                }
            }
            if (atMultivalued) {
                cmd = [...cmd, "--multi-value"];
            } else {
                cmd = [...cmd, "--single-value"];
            }
            if (atNoUserMod) {
                cmd = [...cmd, "--no-user-mod"];
            } else {
                cmd = [...cmd, "--user-mod"];
            }
            cmd = [...cmd, "--oid", atOID];
            cmd = [...cmd, "--usage", atUsage];
            cmd = [...cmd, "--desc", atDesc];

            cmd = [...cmd, "--sup"];
            if (atParent != "") {
                cmd = [...cmd, atParent[0].label];
            } else {
                cmd = [...cmd, ""];
            }

            cmd = [...cmd, "--equality"];
            if (atEqMr != "") {
                cmd = [...cmd, atEqMr[0].label];
            } else {
                cmd = [...cmd, ""];
            }

            cmd = [...cmd, "--substr"];
            if (atSubMr != "") {
                cmd = [...cmd, atSubMr[0].label];
            } else {
                cmd = [...cmd, ""];
            }

            cmd = [...cmd, "--ordering"];
            if (atOrder != "") {
                cmd = [...cmd, atOrder[0].label];
            } else {
                cmd = [...cmd, ""];
            }

            this.toggleLoading("atModal");
            log_cmd("cmdOperationAttribute", `Do the ${action} operation on Attribute`, cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info("cmdOperationAttribute", "Result", content);
                        this.props.addNotification(
                            "success",
                            `Attribute ${atName} - ${action} operation was successfull`
                        );
                        this.loadSchemaData();
                        this.closeAttributeModal();
                        this.toggleLoading("atModal");
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the Attribute ${action} operation - ${errMsg.desc}`
                        );
                        this.loadSchemaData();
                        this.closeAttributeModal();
                        this.toggleLoading("atModal");
                    });
        }
    }

    handleNavSelect(key) {
        this.setState({
            activeKey: key
        });
    }

    handleTypeaheadChange(state, values) {
        this.setState({
            [state]: values
        });
    }

    handleFieldChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        if (e.target.id == "ocUserDefined" && value) {
            this.setState({
                filteredObjectclassRows: searchFilter(
                    "user defined",
                    ["x_origin"],
                    this.state.objectclassRows
                )
            });
        } else {
            this.setState({
                filteredObjectclassRows: this.state.objectclassRows
            });
        }
        if (e.target.id == "atUserDefined" && value) {
            this.setState({
                filteredAttributesRows: searchFilter(
                    "user defined",
                    ["x_origin"],
                    this.state.attributesRows
                )
            });
        } else {
            this.setState({
                filteredAttributesRows: this.state.attributesRows
            });
        }
        this.setState({
            [e.target.id]: value
        });
    }

    render() {
        let schemaPage = "";
        if (this.state.loading) {
            schemaPage = (
                <div className="ds-loading-spinner ds-center">
                    <p />
                    <h4>Loading schema information ...</h4>
                    <Spinner loading size="md" />
                </div>
            );
        } else {
            schemaPage = (
                <div className="container-fluid">
                    <div className="ds-tab-table">
                        <TabContainer
                            id="basic-tabs-pf"
                            onSelect={this.handleNavSelect}
                            activeKey={this.state.activeKey}
                        >
                            <div>
                                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                                    <NavItem eventKey={1}>
                                        <div
                                            dangerouslySetInnerHTML={{ __html: "Objectclasses" }}
                                        />
                                    </NavItem>
                                    <NavItem eventKey={2}>
                                        <div dangerouslySetInnerHTML={{ __html: "Attributes" }} />
                                    </NavItem>
                                    <NavItem eventKey={3}>
                                        <div
                                            dangerouslySetInnerHTML={{ __html: "Matching Rules" }}
                                        />
                                    </NavItem>
                                </Nav>
                                <TabContent>
                                    <TabPane eventKey={1}>
                                        <div className="ds-margin-top-xlg ds-indent">
                                            <Checkbox
                                                id="ocUserDefined"
                                                checked={this.state.ocUserDefined}
                                                title="Show only the objectclasses that are defined by a user"
                                                onChange={this.handleFieldChange}
                                            >
                                                Only Non-standard Schema (objectClasses with
                                                X-ORIGIN: "user defined")
                                            </Checkbox>
                                            <hr />
                                            <ObjectClassesTable
                                                rows={this.state.filteredObjectclassRows}
                                                viewModalHandler={this.showViewObjectclassModal}
                                                editModalHandler={this.showEditObjectclassModal}
                                                deleteHandler={this.showConfirmOCDelete}
                                                loading={this.state.ocTableLoading}
                                            />
                                            <Button
                                                className="ds-margin-top"
                                                bsStyle="primary"
                                                onClick={this.showAddObjectclassModal}
                                            >
                                                Add ObjectClass
                                            </Button>
                                            <ObjectClassModal
                                                addHandler={this.addObjectclass}
                                                editHandler={this.editObjectclass}
                                                newOcEntry={this.state.newOcEntry}
                                                ocModalViewOnly={this.state.ocModalViewOnly}
                                                handleTypeaheadChange={this.handleTypeaheadChange}
                                                handleFieldChange={this.handleFieldChange}
                                                objectclasses={this.state.objectclasses}
                                                attributes={this.state.attributes}
                                                ocName={this.state.ocName}
                                                ocDesc={this.state.ocDesc}
                                                ocOID={this.state.ocOID}
                                                ocParent={this.state.ocParent}
                                                ocKind={this.state.ocKind}
                                                ocMust={this.state.ocMust}
                                                ocMay={this.state.ocMay}
                                                objectclassModalShow={
                                                    this.state.objectclassModalShow
                                                }
                                                closeModal={this.closeObjectclassModal}
                                                loading={this.state.ocModalLoading}
                                            />
                                        </div>
                                    </TabPane>

                                    <TabPane eventKey={2}>
                                        <div className="ds-margin-top-xlg ds-indent">
                                            <Checkbox
                                                id="atUserDefined"
                                                checked={this.state.atUserDefined}
                                                title="Show only the attributes that are defined by a user"
                                                onChange={this.handleFieldChange}
                                            >
                                                Only Non-standard Schema (attributes with X-ORIGIN:
                                                "user defined")
                                            </Checkbox>
                                            <hr />
                                            <AttributesTable
                                                rows={this.state.filteredAttributesRows}
                                                viewModalHandler={this.showViewAttributeModal}
                                                editModalHandler={this.showEditAttributeModal}
                                                deleteHandler={this.showConfirmAttrDelete}
                                                syntaxes={this.state.syntaxes}
                                                loading={this.state.atTableLoading}
                                            />
                                            <Button
                                                className="ds-margin-top"
                                                bsStyle="primary"
                                                onClick={this.showAddAttributeModal}
                                            >
                                                Add Attribute
                                            </Button>
                                            <AttributeTypeModal
                                                addHandler={this.addAttribute}
                                                editHandler={this.editAttribute}
                                                newAtEntry={this.state.newAtEntry}
                                                atModalViewOnly={this.state.atModalViewOnly}
                                                handleTypeaheadChange={this.handleTypeaheadChange}
                                                handleFieldChange={this.handleFieldChange}
                                                objectclasses={this.state.objectclasses}
                                                attributes={this.state.attributes}
                                                matchingrules={this.state.matchingrules}
                                                syntaxes={this.state.syntaxes}
                                                atName={this.state.atName}
                                                atDesc={this.state.atDesc}
                                                atOID={this.state.atOID}
                                                atParent={this.state.atParent}
                                                atSyntax={this.state.atSyntax}
                                                atUsage={this.state.atUsage}
                                                atMultivalued={this.state.atMultivalued}
                                                atNoUserMod={this.state.atNoUserMod}
                                                atAlias={this.state.atAlias}
                                                atEqMr={this.state.atEqMr}
                                                atOrder={this.state.atOrder}
                                                atSubMr={this.state.atSubMr}
                                                attributeModalShow={this.state.attributeModalShow}
                                                closeModal={this.closeAttributeModal}
                                                loading={this.state.atModalLoading}
                                            />
                                        </div>
                                    </TabPane>

                                    <TabPane eventKey={3}>
                                        <div className="ds-margin-top-xlg ds-indent">
                                            <MatchingRulesTable
                                                rows={this.state.matchingrulesRows}
                                            />
                                        </div>
                                    </TabPane>
                                </TabContent>
                            </div>
                        </TabContainer>
                    </div>
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmDeleteOC}
                        closeHandler={this.closeConfirmOCDelete}
                        handleChange={this.handleFieldChange}
                        actionHandler={this.doDeleteOC}
                        spinning={this.state.modalSpinning}
                        item={this.state.deleteName}
                        checked={this.state.modalChecked}
                        mTitle="Delete An Objectclass"
                        mMsg="Are you sure you want to delete this Objectclass?"
                        mSpinningMsg="Deleting objectclass ..."
                        mBtnName="Delete Objectclass"
                    />
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmAttrDelete}
                        closeHandler={this.closeConfirmAttrDelete}
                        handleChange={this.handleFieldChange}
                        actionHandler={this.doDeleteAttr}
                        spinning={this.state.modalSpinning}
                        item={this.state.deleteName}
                        checked={this.state.modalChecked}
                        mTitle="Delete An Attribute"
                        mMsg="Are you sure you want to delete this Attribute?"
                        mSpinningMsg="Deleting attribute ..."
                        mBtnName="Delete Attribute"
                    />
                </div>
            );
        }
        return <div>{schemaPage}</div>;
    }
}

// Props and defaultProps

Schema.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

Schema.defaultProps = {
    addNotification: noop,
    serverId: ""
};

export default Schema;
