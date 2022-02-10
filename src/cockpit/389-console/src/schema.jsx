import cockpit from "cockpit";
import React from "react";
import { log_cmd, searchFilter, listsEqual } from "./lib/tools.jsx";
import {
    ObjectClassesTable,
    AttributesTable,
    MatchingRulesTable
} from "./lib/schema/schemaTables.jsx";
import { ObjectClassModal, AttributeTypeModal } from "./lib/schema/schemaModals.jsx";
import { DoubleConfirmModal } from "./lib/notifications.jsx";
import {
    Button,
    Checkbox,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
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
            activeTabKey: 0,

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
            ocTableKey: 0,
            attrTableKey: 0,
            errObj: {},
            saveBtnDisabled: true,

            ocModalViewOnly: false,
            ocName: "",
            ocDesc: "",
            ocOID: "",
            ocParent: "",
            ocParentOptions: [],
            ocKind: "",
            ocMust: [],
            ocMustOptions: [],
            ocMay: [],
            ocMayOptions: [],
            ocUserDefined: false,
            objectclassModalShow: false,
            newOcEntry: true,
            ocTableLoading: false,
            ocModalLoading: false,

            atName: "",
            atDesc: "",
            atOID: "",
            atParent: "",
            atParentOptions: [],
            atSyntax: "",
            atUsage: "userApplications",
            atMultivalued: false,
            atNoUserMod: false,
            atAlias: [],
            atAliasOptions: [],
            atEqMr: "",
            atOrder: "",
            atSubMr: "",
            atUserDefined: false,
            atModalViewOnly: false,
            attributeModalShow: false,
            newAtEntry: true,
            atTableLoading: false,
            atModalLoading: false,

            isParentObjOpen: false,
            isRequiredAttrsOpen: false,
            isAllowedAttrsOpen: false,

            isParentAttrOpen: false,
            isAliasNameOpen: false,
            isEqualityMROpen: false,
            isOrderMROpen: false,
            isSubstringMROpen: false,
        };

        // Substring Matching Rule
        this.handleSubstringMRToggle = isSubstringMROpen => {
            this.setState({
                isSubstringMROpen
            });
        };
        this.handleSubstringMRClear = () => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            this.setState({
                atSubMr: "",
                isSubstringMROpen: false
            }, () => { this.onAttrChange(e) });
        };
        this.handleSubstringMRSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.atSubMr === selection) {
                this.setState({
                    atSubMr: selection,
                    isSubstringMROpen: false
                }, () => { this.onAttrChange(e) });
            } else {
                this.setState({
                    atSubMr: selection,
                    isSubstringMROpen: false
                }, () => { this.onAttrChange(e) });
            }
        };

        // Order Matching Rule
        this.handleOrderMRToggle = isOrderMROpen => {
            this.setState({
                isOrderMROpen
            });
        };
        this.handleOrderMRClear = () => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            this.setState({
                atOrder: "",
                isOrderMROpen: false
            }, () => { this.onAttrChange(e) });
        };
        this.handleOrderMRSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.atOrder === selection) {
                this.setState({
                    atOrder: "",
                    isOrderMROpen: false
                }, () => { this.onAttrChange(e) });
            } else {
                this.setState({
                    atOrder: selection,
                    isOrderMROpen: false
                }, () => { this.onAttrChange(e) });
            }
        };

        // Equaliry Matching Rule
        this.handleEqualityMRToggle = isEqualityMROpen => {
            this.setState({
                isEqualityMROpen
            });
        };
        this.handleEqualityMRClear = () => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            this.setState({
                atEqMr: "",
                isEqualityMROpen: false
            }, () => { this.onAttrChange(e) });
        };
        this.handleEqualityMRSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.atEqMr === selection) {
                this.setState({
                    atEqMr: "",
                    isEqualityMROpen: false
                }, () => { this.onAttrChange(e) });
            } else {
                this.setState({
                    atEqMr: selection,
                    isEqualityMROpen: false
                }, () => { this.onAttrChange(e) });
            }
        };

        // Alias Name
        this.handleAliasNameToggle = isAliasNameOpen => {
            this.setState({
                isAliasNameOpen
            });
        };
        this.handleAliasNameClear = () => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            this.setState({
                atAlias: [],
                isAliasNameOpen: false
            }, () => { this.onAttrChange(e) });
        };
        this.handleAliasNameSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.atAlias.includes(selection)) {
                this.setState(
                    prevState => ({
                        atAlias: prevState.atAlias.filter((item) => item !== selection),
                        isAliasNameOpen: false
                    }), () => { this.onAttrChange(e) }
                );
            } else {
                this.setState(
                    prevState => ({
                        atAlias: [...prevState.atAlias, selection],
                        isAliasNameOpen: false
                    }), () => { this.onAttrChange(e) }
                );
            }
        };
        this.handleAliasNameCreateOption = newValue => {
            if (!this.state.atAliasOptions.includes(newValue)) {
                this.setState({
                    atAliasOptions: [...this.state.atAliasOptions, { value: newValue }]
                });
            }
        };

        // Parent Attribute
        this.handleParentAttrToggle = isParentAttrOpen => {
            this.setState({
                isParentAttrOpen
            });
        };
        this.handleParentAttrClear = () => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            this.setState({
                atParent: "",
                isParentAttrOpen: false
            }, () => { this.onAttrChange(e) });
        };
        this.handleParentAttrSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.atParent === selection) {
                this.setState({
                    atParent: "",
                    isParentAttrOpen: false
                }, () => { this.onAttrChange(e) });
            } else {
                this.setState(
                    prevState => ({
                        atParent: selection,
                        isParentAttrOpen: false
                    }), () => { this.onAttrChange(e) }
                );
            }
        };

        // Required Attributes
        this.handleRequiredAttrsToggle = isRequiredAttrsOpen => {
            this.setState({
                isRequiredAttrsOpen
            });
        };
        this.handleRequiredAttrsClear = () => {
            this.setState({
                ocMust: [],
                isRequiredAttrsOpen: false
            });
        };
        this.handleRequiredAttrsSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.ocMust.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        ocMust: prevState.ocMust.filter((item) => item !== selection),
                        isRequiredAttrsOpen: false
                    }), () => { this.onOCChange(e) }
                );
            } else {
                this.setState(
                    prevState => ({
                        ocMust: [...prevState.ocMust, selection],
                        isRequiredAttrsOpen: false
                    }), () => { this.onOCChange(e) }
                );
            }
        };
        this.handleRequiredAttrsCreateOption = newValue => {
            if (!this.state.ocMustOptions.includes(newValue)) {
                this.setState({
                    ocMustOptions: [...this.state.ocParentocMustOptionsOptions, { value: newValue }]
                });
            }
        };

        // Allowed Attributes
        this.handleAllowedAttrsToggle = isAllowedAttrsOpen => {
            this.setState({
                isAllowedAttrsOpen
            });
        };
        this.handleAllowedAttrsClear = () => {
            this.setState({
                ocMay: [],
                isAllowedAttrsOpen: false
            });
        };
        this.handleAllowedAttrsSelect = (event, selection) => {
            const e = { target: { id: 'dummy', value: "", type: 'input' } };
            if (this.state.ocMay.includes(selection)) {
                this.setState(
                    prevState => ({
                        ocMay: prevState.ocMay.filter((item) => item !== selection),
                        isAllowedAttrsOpen: false
                    }), () => { this.onOCChange(e) }
                );
            } else {
                this.setState(
                    prevState => ({
                        ocMay: [...prevState.ocMay, selection],
                        isAllowedAttrsOpen: false
                    }), () => { this.onOCChange(e) }
                );
            }
        };
        this.handleAllowedAttrsCreateOption = newValue => {
            if (!this.state.ocMayOptions.includes(newValue)) {
                this.setState({
                    ocMayOptions: [...this.state.ocMayOptions, { value: newValue }]
                });
            }
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            event.preventDefault();
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.validateForm = this.validateForm.bind(this);
        this.onAttrChange = this.onAttrChange.bind(this);
        this.onOCChange = this.onOCChange.bind(this);
        this.onFieldChange = this.onFieldChange.bind(this);
        this.loadSchemaData = this.loadSchemaData.bind(this);
        this.loadSyntaxesFirst = this.loadSyntaxesFirst.bind(this);
        this.toggleLoading = this.toggleLoading.bind(this);

        this.showEditObjectclassModal = this.showEditObjectclassModal.bind(this);
        this.handleShowAddObjectclassModal = this.handleShowAddObjectclassModal.bind(this);
        this.openObjectclassModal = this.openObjectclassModal.bind(this);
        this.closeObjectclassModal = this.closeObjectclassModal.bind(this);
        this.doDeleteOC = this.doDeleteOC.bind(this);
        this.addObjectclass = this.addObjectclass.bind(this);
        this.editObjectclass = this.editObjectclass.bind(this);
        this.cmdOperationObjectclass = this.cmdOperationObjectclass.bind(this);
        this.showConfirmOCDelete = this.showConfirmOCDelete.bind(this);
        this.closeConfirmOCDelete = this.closeConfirmOCDelete.bind(this);

        this.showEditAttributeModal = this.showEditAttributeModal.bind(this);
        this.handleShowAddAttributeModal = this.handleShowAddAttributeModal.bind(this);
        this.openAttributeModal = this.openAttributeModal.bind(this);
        this.closeAttributeModal = this.closeAttributeModal.bind(this);
        this.doDeleteAttr = this.doDeleteAttr.bind(this);
        this.addAttribute = this.addAttribute.bind(this);
        this.editAttribute = this.editAttribute.bind(this);
        this.showConfirmAttrDelete = this.showConfirmAttrDelete.bind(this);
        this.closeConfirmAttrDelete = this.closeConfirmAttrDelete.bind(this);
    }

    toggleLoading(item) {
        if (item === "allSchema") {
            this.setState(prevState => ({
                loading: !prevState.loading
            }));
        } else if (item === "ocTable") {
            this.setState(prevState => ({
                ocTableLoading: !prevState.ocTableLoading
            }));
        } else if (item === "ocModal") {
            this.setState(prevState => ({
                ocModalLoading: !prevState.ocModalLoading
            }));
        } else if (item === "atTable") {
            this.setState(prevState => ({
                atTableLoading: !prevState.atTableLoading
            }));
        } else if (item === "atModal") {
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
        const cmd = [
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
                    const myObject = JSON.parse(content);
                    this.setState({
                        syntaxes: myObject.items
                    });
                    this.loadSchemaData(true);
                })
                .fail(err => {
                    if (err !== 0) {
                        const errMsg = JSON.parse(err);
                        console.log("loadSyntaxes failed: ", errMsg.desc);
                    }
                });
    }

    loadSchemaData(initialLoading) {
        const cmd = [
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
                    const myObject = JSON.parse(content);
                    const attrs = [];
                    const ocs = [];
                    const mrs = [];
                    const ocKey = this.state.ocTableKey + 1;
                    const attrKey = this.state.attrTableKey + 1;
                    for (const content of myObject.attributetypes.items) {
                        attrs.push(content.name[0]);
                    }
                    for (const content of myObject.objectclasses.items) {
                        ocs.push(content.name[0]);
                    }
                    for (const content of myObject.matchingrules.items) {
                        if (content.name[0] !== "") {
                            mrs.push(content.name[0]);
                        } else {
                            content.name[0] = <i>&lt;No Name&gt;</i>;
                        }
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
                            ),
                            ocTableKey: ocKey
                        });
                    } else {
                        this.setState({
                            filteredObjectclassRows: myObject.objectclasses.items,
                            ocTableKey: ocKey
                        });
                    }
                    if (this.state.atUserDefined) {
                        this.setState({
                            filteredAttributesRows: searchFilter(
                                "user defined",
                                ["x_origin"],
                                myObject.attributetypes.items
                            ),
                            attrTableKey: attrKey
                        });
                    } else {
                        this.setState({
                            filteredAttributesRows: myObject.attributetypes.items,
                            attrTableKey: attrKey
                        });
                    }
                    if (initialLoading) {
                        this.toggleLoading("allSchema");
                    } else {
                        this.setState({
                            atTableLoading: false,
                            ocTableLoading: false,
                        });
                    }
                })
                .fail(err => {
                    if (err !== 0) {
                        const errMsg = JSON.parse(err);
                        console.log("loadSchemaData failed: ", errMsg.desc);
                    }
                    if (initialLoading) {
                        this.toggleLoading("allSchema");
                    } else {
                        this.setState({
                            atTableLoading: false,
                            ocTableLoading: false,
                        });
                    }
                });
    }

    showEditObjectclassModal(name) {
        this.setState({
            ocModalViewOnly: false,
            saveBtnDisabled: true,
            errObj: {},
        });
        this.openObjectclassModal(name);
    }

    handleShowAddObjectclassModal(rowData) {
        this.setState({
            ocModalViewOnly: false,
            saveBtnDisabled: true,
            errObj: { ocName: true, ocDesc: true },
        });
        this.openObjectclassModal();
    }

    openObjectclassModal(name) {
        if (!name) {
            this.setState({
                ocName: "",
                ocDesc: "",
                ocOID: "",
                ocParent: "top",
                ocKind: "",
                ocMust: [],
                ocMay: [],
                objectclassModalShow: true,
                newOcEntry: true,
            });
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "schema",
                "objectclasses",
                "query",
                name
            ];

            log_cmd("openObjectclassModal", "Fetch ObjectClass data from schema", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const obj = JSON.parse(content);
                        const item = obj.oc;
                        let ocMustList = [];
                        let ocMayList = [];
                        const kindOpts = ["STRUCTURAL", "ABSTRACT", "AUXILIARY"];
                        this.setState({
                            objectclassModalShow: true,
                            newOcEntry: false,
                            ocName: item.name === undefined ? "" : item.name[0],
                            ocDesc: item.desc === null ? "" : item.desc[0],
                            ocOID: item.oid === undefined ? "" : item.oid[0],
                            ocKind: item.kind === undefined ? "" : kindOpts[item.kind],
                            ocParent:
                            item.sup === undefined
                                ? []
                                : item.sup[0],
                            // Store original values
                            _ocName: item.name === undefined ? "" : item.name[0],
                            _ocDesc: item.desc === null ? "" : item.desc[0],
                            _ocOID: item.oid === undefined ? "" : item.oid[0],
                            _ocKind: item.kind === undefined ? "" : kindOpts[item.kind],
                            _ocParent:
                            item.sup === undefined
                                ? []
                                : item.sup[0]
                        });
                        if (item.must === undefined) {
                            this.setState({ ocMust: [], _ocMust: [] });
                        } else {
                            for (const value of item.must) {
                                ocMustList = [...ocMustList, value];
                            }
                            this.setState({
                                ocMust: ocMustList,
                                _ocMust: ocMustList
                            });
                        }
                        if (item.may === undefined) {
                            this.setState({ ocMay: [], _ocMay: [] });
                        } else {
                            for (const value of item.may) {
                                ocMayList = [...ocMayList, value];
                            }
                            this.setState({
                                ocMay: ocMayList,
                                _ocMay: ocMayList
                            });
                        }
                    })
                    .fail(_ => {
                        this.setState({
                            ocName: "",
                            ocDesc: "",
                            ocOID: "",
                            ocParent: "",
                            ocKind: "",
                            ocMust: [],
                            ocMay: [],
                            objectclassModalShow: true,
                            newOcEntry: true
                        });
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
        const cmd = [
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

        this.toggleLoading("ocTable");
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
                    const errMsg = JSON.parse(err);
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
        if (ocName === "") {
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
            if (ocOID !== "") {
                cmd = [...cmd, "--oid", ocOID];
            }
            if (ocParent !== "") {
                cmd = [...cmd, "--sup", ocParent];
            }
            if (ocKind !== "") {
                cmd = [...cmd, "--kind", ocKind];
            }
            if (ocDesc !== "") {
                cmd = [...cmd, "--desc", ocDesc];
            }
            if (ocMust.length !== 0) {
                cmd = [...cmd, "--must"];
                for (const value of ocMust) {
                    cmd = [...cmd, value];
                }
            }
            if (ocMay.length !== 0) {
                cmd = [...cmd, "--may"];
                for (const value of ocMay) {
                    cmd = [...cmd, value];
                }
            }

            this.toggleLoading("ocModal");
            this.toggleLoading("ocTable");
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
                        if ('info' in errMsg) {
                            errMsg = errMsg.desc + " " + errMsg.info;
                        } else {
                            errMsg = errMsg.desc;
                        }
                        this.props.addNotification(
                            "error",
                            `Error during the ObjectClass ${action} operation - ${errMsg}`
                        );
                        this.loadSchemaData();
                        this.closeObjectclassModal();
                        this.toggleLoading("ocModal");
                    });
        }
    }

    showEditAttributeModal(name) {
        this.setState({
            atModalViewOnly: false,
            saveBtnDisabled: true,
            errObj: {},
        });
        this.openAttributeModal(name);
    }

    handleShowAddAttributeModal(rowData) {
        this.setState({
            atModalViewOnly: false,
            saveBtnDisabled: true,
            errObj: { atName: true, atDesc: true },
        });
        this.openAttributeModal();
    }

    openAttributeModal(name) {
        if (!name) {
            this.setState({
                atName: "",
                atDesc: "",
                atOID: "",
                atParent: "",
                atSyntax: "1.3.6.1.4.1.1466.115.121.1.15",
                atUsage: "userApplications",
                atMultivalued: false,
                atNoUserMod: false,
                atAlias: [],
                atEqMr: "",
                atOrder: "",
                atSubMr: "",
                attributeModalShow: true,
                newAtEntry: true
            });
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "schema",
                "attributetypes",
                "query",
                name
            ];

            log_cmd("openAttributeModal", "Fetch Attribute data from schema", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const obj = JSON.parse(content);
                        const item = obj.at;
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
                            atName: item.name === undefined ? "" : item.name[0],
                            atDesc: item.desc === null ? "" : item.desc[0],
                            atOID: item.oid === undefined ? "" : item.oid[0],
                            atParent: item.sup.length === 0 ? "" : item.sup[0],
                            atSyntax:
                            item.syntax === undefined
                                ? ""
                                : item.syntax[0],
                            atUsage: item.usage === undefined ? "" : atUsageOpts[item.usage],
                            atMultivalued: !item.single_value,
                            atNoUserMod: item.no_user_mod,
                            atEqMr:
                            item.equality === null
                                ? ""
                                : item.equality[0],
                            atOrder:
                            item.ordering === null
                                ? ""
                                : item.ordering[0],
                            atSubMr:
                            item.substr === null
                                ? ""
                                : item.substr[0],

                            // store orig valuses
                            _atName: item.name === undefined ? "" : item.name[0],
                            _atDesc: item.desc === null ? "" : item.desc[0],
                            _atOID: item.oid === undefined ? "" : item.oid[0],
                            _atParent: item.sup[0],
                            _atSyntax:
                            item.syntax === undefined
                                ? ""
                                : item.syntax[0],
                            _atUsage: item.usage === undefined ? "" : atUsageOpts[item.usage],
                            _atMultivalued: !item.single_value,
                            _atNoUserMod: item.no_user_mod,
                            _atEqMr:
                            item.equality === null
                                ? ""
                                : item.equality[0],
                            _atOrder:
                            item.ordering === null
                                ? ""
                                : item.ordering[0],
                            _atSubMr:
                            item.substr === null
                                ? ""
                                : item.substr[0],
                        });
                        if (item.aliases === null) {
                            this.setState({ atAlias: [], _atAlias: [] });
                        } else {
                            for (const value of item.aliases) {
                                atAliasList = [...atAliasList, value];
                            }
                            this.setState({
                                atAlias: atAliasList,
                                _atAlias: atAliasList,
                            });
                        }
                    })
                    .fail(_ => {
                        this.setState({
                            atName: "",
                            atDesc: "",
                            atOID: "",
                            atParent: "",
                            atSyntax: "",
                            atUsage: "userApplications",
                            atMultivalued: false,
                            atNoUserMod: false,
                            atAlias: [],
                            atEqMr: "",
                            atOrder: "",
                            atSubMr: "",
                            attributeModalShow: true,
                            newAtEntry: true
                        });
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
        const cmd = [
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

        this.toggleLoading("atTable");
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
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during Attribute removal operation - ${errMsg.desc}`
                    );
                    this.loadSchemaData();
                    this.closeConfirmAttrDelete();
                });
    }

    addAttribute() {
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

        const eqMR = atEqMr;
        const orderMR = atOrder;
        const subMR = atSubMr;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "add",
            atName
        ];

        cmd = [...cmd, "--syntax", atSyntax];
        if (atAlias.length !== 0) {
            cmd = [...cmd, "--aliases"];
            for (const value of atAlias) {
                cmd = [...cmd, value];
            }
        }

        if (atParent !== "") {
            cmd = [...cmd, "--sup", atParent];
        }

        if (eqMR !== "") {
            cmd = [...cmd, "--equality", eqMR];
        }
        if (subMR !== "") {
            cmd = [...cmd, "--substr", subMR];
        }
        if (orderMR !== "") {
            cmd = [...cmd, "--ordering", orderMR];
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
        if (atOID !== "") {
            cmd = [...cmd, "--oid", atOID];
        }
        if (atUsage !== "") {
            cmd = [...cmd, "--usage", atUsage];
        }
        if (atDesc !== "") {
            cmd = [...cmd, "--desc", atDesc];
        }

        this.toggleLoading("atModal");
        this.toggleLoading("atTable");
        log_cmd("cmdOperationAttribute", `Do the add operation on Attribute`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("cmdOperationAttribute", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Attribute ${atName} - add operation was successfull`
                    );
                    this.loadSchemaData();
                    this.closeAttributeModal();
                    this.toggleLoading("atModal");
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if ('info' in errMsg) {
                        errMsg = errMsg.desc + " " + errMsg.info;
                    } else {
                        errMsg = errMsg.desc;
                    }
                    this.props.addNotification(
                        "error",
                        `Error during the Attribute add operation - ${errMsg}`
                    );
                    this.loadSchemaData();
                    this.closeAttributeModal();
                    this.toggleLoading("atModal");
                });
    }

    editAttribute(action) {
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

        let eqMR = atEqMr;
        let orderMR = atOrder;
        let subMR = atSubMr;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "replace",
            atName
        ];

        cmd = [...cmd, "--syntax", atSyntax];
        if (!listsEqual(atAlias, this.state._atAlias)) {
            cmd = [...cmd, "--aliases"];
            for (const value of atAlias) {
                cmd = [...cmd, value];
            }
        }

        if (atParent !== "") {
            cmd = [...cmd, "--sup", atParent];
        } else if (this.state._atParent !== "") {
            // Removed the parent attribute, so we need to remove the matching rules
            eqMR = "";
            orderMR = "";
            subMR = "";
        }

        if (eqMR !== this.state._atEqMr) {
            cmd = [...cmd, "--equality", eqMR];
        }
        if (subMR !== this.state._atSubMr) {
            cmd = [...cmd, "--substr", subMR];
        }
        if (orderMR !== this.state._atOrder) {
            cmd = [...cmd, "--ordering", orderMR];
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
        if (atOID !== this.state._atOID) {
            cmd = [...cmd, "--oid", atOID];
        }
        if (atUsage !== this.state._atUsage) {
            cmd = [...cmd, "--usage", atUsage];
        }
        if (atDesc !== this.state._atDesc) {
            cmd = [...cmd, "--desc", atDesc];
        }

        this.toggleLoading("atModal");
        this.toggleLoading("atTable");
        log_cmd("cmdOperationAttribute", `Do the replace operation on Attribute`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("cmdOperationAttribute", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Attribute ${atName} - replace operation was successfull`
                    );
                    this.loadSchemaData();
                    this.closeAttributeModal();
                    this.toggleLoading("atModal");
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if ('info' in errMsg) {
                        errMsg = errMsg.desc + " " + errMsg.info;
                    } else {
                        errMsg = errMsg.desc;
                    }
                    this.props.addNotification(
                        "error",
                        `Error during the Attribute replace operation - ${errMsg}`
                    );
                    this.loadSchemaData();
                    this.closeAttributeModal();
                    this.toggleLoading("atModal");
                });
    }

    validateForm(attr, value, attrs, errObj) {
        let all_good = true;
        for (const check_attr of attrs) {
            errObj[check_attr] = false;
            if (attr !== check_attr) {
                if (this.state[check_attr] === "") {
                    errObj[check_attr] = true;
                    all_good = false;
                }
            } else if (value === "") {
                errObj[check_attr] = true;
                all_good = false;
            }
        }

        return all_good;
    }

    onAttrChange (e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let saveBtnDisabled = true;
        const errObj = this.state.errObj;
        let all_good;

        const attrs = [
            'atName', 'atDesc', 'atOID', 'atMultivalued', 'atNoUserMod',
            'atSyntax', 'atEqMr', 'atOrder', 'atSubMr', 'atParent'
        ];
        const attrLists = [
            'atAlias'
        ];

        if (this.state.newAtEntry) {
            // Add Form
            if (this.validateForm(attr, value, ['atName', 'atDesc', 'atSyntax'], errObj)) {
                // Form is good to save
                saveBtnDisabled = false;
            }
        } else {
            // Edit
            all_good = this.validateForm(attr, value, ['atName', 'atDesc', 'atSyntax'], errObj);
            if (all_good) {
                // Check for difference before enabling save btn
                for (const check_attr of attrs) {
                    if (attr !== check_attr) {
                        if (this.state[check_attr] !== this.state['_' + check_attr]) {
                            saveBtnDisabled = false;
                            break;
                        }
                    } else if (value !== this.state['_' + check_attr]) {
                        saveBtnDisabled = false;
                        break;
                    }
                }
                for (const check_attr of attrLists) {
                    if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                        saveBtnDisabled = false;
                        break;
                    }
                }
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled: saveBtnDisabled,
            errObj: errObj
        });
    }

    onOCChange (e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let saveBtnDisabled = true;
        const errObj = this.state.errObj;
        let all_good;

        const attrs = [
            'ocName', 'ocDesc', 'ocOID', 'ocParent'
        ];
        const attrLists = [
            'ocMust', 'ocMay'
        ];

        if (this.state.newOcEntry) {
            // Add OC Form
            if (this.validateForm(attr, value, ['ocName', 'ocDesc', 'ocParent'], errObj)) {
                // Form is good to save
                saveBtnDisabled = false;
            }
        } else {
            // Edit
            all_good = this.validateForm(attr, value, ['ocName', 'ocDesc', 'ocParent'], errObj);
            if (all_good) {
                // Check for difference before enabling save btn
                for (const check_attr of attrs) {
                    if (attr !== check_attr) {
                        if (this.state[check_attr] !== this.state['_' + check_attr]) {
                            saveBtnDisabled = false;
                            break;
                        }
                    } else if (value !== this.state['_' + check_attr]) {
                        saveBtnDisabled = false;
                        break;
                    }
                }
                for (const check_attr of attrLists) {
                    if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                        saveBtnDisabled = false;
                        break;
                    }
                }
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled: saveBtnDisabled,
            errObj: errObj
        });
    }

    onFieldChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        const ocKey = this.state.ocTableKey + 1;
        const attrKey = this.state.attrTableKey + 1;
        if (e.target.id === "ocUserDefined") {
            if (value) {
                this.setState({
                    filteredObjectclassRows: searchFilter(
                        "user defined",
                        ["x_origin"],
                        this.state.objectclassRows
                    ),
                    ocTableKey: ocKey
                });
            } else {
                this.setState({
                    filteredObjectclassRows: this.state.objectclassRows,
                    ocTableKey: ocKey
                });
            }
        } else if (e.target.id === "atUserDefined") {
            if (value) {
                this.setState({
                    filteredAttributesRows: searchFilter(
                        "user defined",
                        ["x_origin"],
                        this.state.attributesRows
                    ),
                    attrTableKey: attrKey
                });
            } else {
                this.setState({
                    filteredAttributesRows: this.state.attributesRows,
                    attrTableKey: attrKey
                });
            }
        }

        this.setState({
            [e.target.id]: value
        });
    }

    render() {
        let schemaPage = "";
        if (this.state.loading) {
            schemaPage = (
                <div className="ds-center ds-margin-top-xlg">
                    <TextContent>
                        <Text component={TextVariants.h3}>Loading Schema Information ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="xl" />
                </div>
            );
        } else {
            schemaPage = (
                <div className="ds-indent ds-margin-top-xlg ds-margin-bottom-md">
                    <Tabs isBox activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>Objectclasses</TabTitleText>}>
                            <div className="ds-margin-top-xlg ds-indent">
                                <Checkbox
                                    id="ocUserDefined"
                                    isChecked={this.state.ocUserDefined}
                                    title="Show only the objectclasses that are defined by a user and have the X-ORIGIN set to 'user defined'"
                                    onChange={(checked, e) => {
                                        this.onFieldChange(e);
                                    }}
                                    label="Only Show Non-standard/Custom Schema"
                                />
                                <ObjectClassesTable
                                    className="ds-margin-top-lg"
                                    key={this.state.ocTableKey}
                                    rows={this.state.filteredObjectclassRows}
                                    editModalHandler={this.showEditObjectclassModal}
                                    deleteHandler={this.showConfirmOCDelete}
                                    loading={this.state.ocTableLoading}
                                />
                                <Button
                                    variant="primary"
                                    onClick={this.handleShowAddObjectclassModal}
                                >
                                    Add ObjectClass
                                </Button>
                                <ObjectClassModal
                                    addHandler={this.addObjectclass}
                                    editHandler={this.editObjectclass}
                                    newOcEntry={this.state.newOcEntry}
                                    ocModalViewOnly={this.state.ocModalViewOnly}
                                    handleFieldChange={this.onOCChange}
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
                                    isParentObjOpen={this.state.isParentObjOpen}
                                    isRequiredAttrsOpen={this.state.isRequiredAttrsOpen}
                                    isAllowedAttrsOpen={this.state.isAllowedAttrsOpen}
                                    onRequiredAttrsToggle={this.handleRequiredAttrsToggle}
                                    onRequiredAttrsClear={this.handleRequiredAttrsClear}
                                    onRequiredAttrsSelect={this.handleRequiredAttrsSelect}
                                    onRequiredAttrsCreateOption={this.handleRequiredAttrsCreateOption}
                                    onAllowedAttrsToggle={this.handleAllowedAttrsToggle}
                                    onAllowedAttrsClear={this.handleAllowedAttrsClear}
                                    onAllowedAttrsSelect={this.handleAllowedAttrsSelect}
                                    onAllowedAttrsCreateOption={this.handleAllowedAttrsCreateOption}
                                    saveBtnDisabled={this.state.saveBtnDisabled}
                                    error={this.state.errObj}
                                />
                            </div>
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>Attributes</TabTitleText>}>
                            <div className="ds-margin-top-xlg ds-indent">
                                <Checkbox
                                    id="atUserDefined"
                                    isChecked={this.state.atUserDefined}
                                    title="Show only the attributes that are defined by a user, and have the X-ORIGIN set to 'user defined'"
                                    onChange={(checked, e) => {
                                        this.onFieldChange(e);
                                    }}
                                    label="Only Show Non-standard/Custom Schema"
                                />
                                <AttributesTable
                                    className="ds-margin-top-lg"
                                    key={this.state.attrTableKey}
                                    rows={this.state.filteredAttributesRows}
                                    editModalHandler={this.showEditAttributeModal}
                                    deleteHandler={this.showConfirmAttrDelete}
                                    syntaxes={this.state.syntaxes}
                                    loading={this.state.atTableLoading}
                                />
                                <Button
                                    variant="primary"
                                    onClick={this.handleShowAddAttributeModal}
                                >
                                    Add Attribute
                                </Button>
                                <AttributeTypeModal
                                    addHandler={this.addAttribute}
                                    editHandler={this.editAttribute}
                                    newAtEntry={this.state.newAtEntry}
                                    atModalViewOnly={this.state.atModalViewOnly}
                                    handleFieldChange={this.onAttrChange}
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
                                    isParentAttrOpen={this.state.isParentAttrOpen}
                                    isAliasNameOpen={this.state.isAliasNameOpen}
                                    isEqualityMROpen={this.state.isEqualityMROpen}
                                    isOrderMROpen={this.state.isOrderMROpen}
                                    isSubstringMROpen={this.state.isSubstringMROpen}
                                    onParentAttrToggle={this.handleParentAttrToggle}
                                    onParentAttrClear={this.handleParentAttrClear}
                                    onParentAttrSelect={this.handleParentAttrSelect}
                                    onAliasNameToggle={this.handleAliasNameToggle}
                                    onAliasNameClear={this.handleAliasNameClear}
                                    onAliasNameSelect={this.handleAliasNameSelect}
                                    onAliasNameCreateOption={this.handleAliasNameCreateOption}
                                    onEqualityMRToggle={this.handleEqualityMRToggle}
                                    onEqualityMRClear={this.handleEqualityMRClear}
                                    onEqualityMRSelect={this.handleEqualityMRSelect}
                                    onOrderMRToggle={this.handleOrderMRToggle}
                                    onOrderMRClear={this.handleOrderMRClear}
                                    onOrderMRSelect={this.handleOrderMRSelect}
                                    onSubstringMRToggle={this.handleSubstringMRToggle}
                                    onSubstringMRClear={this.handleSubstringMRClear}
                                    onSubstringMRSelect={this.handleSubstringMRSelect}
                                    saveBtnDisabled={this.state.saveBtnDisabled}
                                    error={this.state.errObj}
                                />
                            </div>
                        </Tab>
                        <Tab eventKey={2} title={<TabTitleText>Matching Rules</TabTitleText>}>
                            <div className="ds-margin-top-xlg ds-indent">
                                <MatchingRulesTable
                                    rows={this.state.matchingrulesRows}
                                />
                            </div>
                        </Tab>
                    </Tabs>
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmDeleteOC}
                        closeHandler={this.closeConfirmOCDelete}
                        handleChange={this.onFieldChange}
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
                        handleChange={this.onFieldChange}
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
    serverId: ""
};

export default Schema;
