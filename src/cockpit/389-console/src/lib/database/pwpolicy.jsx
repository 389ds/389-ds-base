import cockpit from "cockpit";
import React from "react";
import { NotificationController } from "../notifications.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Form,
    Col,
    Nav,
    NavItem,
    Checkbox,
    TabContainer,
    TabContent,
    TabPane,
    TreeView,
    Spinner,
    Button
} from "patternfly-react";
import "../../css/ds.css";

const GLOBAL_POLICY = "global-policy";

export class ServerPwPolicy extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: false,
            loaded: false,
            activeKey: 1,
            notifications: [],
            disableTree: false,
            nodes: [],
            node_name: "",
            node_text: "",
            dbtype: "",

            // Tuning settings

        }
        this.removeNotification = this.removeNotification.bind(this);
        this.addNotification = this.addNotification.bind(this);
        this.selectNode = this.selectNode.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.savePwp = this.savePwp.bind(this);
        this.loadPwp = this.loadPwp.bind(this);
        this.loadGlobal = this.loadGlobal.bind(this);
    }

    componentWillMount() {
        // Loading config TODO
        if (!this.state.loaded) {
            this.loadGlobal();
        }
    }

    loadGlobal() {
        let cmd = [
            "dsconf", "-j", this.props.serverId, "pwpolicy", "get"
        ];
        log_cmd("loadGlobal", "Load global password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    // Handle the checkbox values
                    let pwpLocal = false;
                    let pwpIsglobal = false;
                    let pwMustChange = false;
                    let pwHistory = false;
                    let pwTrackUpdate = false;
                    let pwExpire = false;
                    let pwSendExpire = false;
                    let pwLockout = false;
                    let pwUnlock = false;
                    let pwCheckSyntax = false;
                    let pwPalindrome = false;
                    let pwDictCheck = false;
                    let pwAllowHashed = false;

                    if (attrs['nsslapd-pwpolicy-local'][0] == "on") {
                        pwpLocal = true;
                    }
                    if (attrs['passwordchange'][0] == "on") {
                        pwMustChange = true;
                    }
                    if (attrs['passwordhistory'][0] == "on") {
                        pwHistory = true;
                    }
                    if (attrs['passwordtrackupdatetime'][0] == "on") {
                        pwTrackUpdate = true;
                    }
                    if (attrs['passwordisglobalpolicy'][0] == "on") {
                        pwpIsglobal = true;
                    }
                    if (attrs['passwordsendexpiringtime'][0] == "on") {
                        pwSendExpire = true;
                    }
                    if (attrs['passwordlockout'][0] == "on") {
                        pwLockout = true;
                    }
                    if (attrs['passwordunlock'][0] == "on") {
                        pwUnlock = true;
                    }
                    if (attrs['passwordexp'][0] == "on") {
                        pwExpire = true;
                    }
                    if (attrs['passwordchecksyntax'][0] == "on") {
                        pwCheckSyntax = true;
                    }
                    if (attrs['passwordpalindrome'][0] == "on") {
                        pwPalindrome = true;
                    }
                    if (attrs['passworddictcheck'][0] == "on") {
                        pwExpire = true;
                    }
                    if (attrs['nsslapd-allow-hashed-passwords'][0] == "on") {
                        pwAllowHashed = true;
                    }

                    this.setState(() => (
                        {
                            loaded: true,
                            loading: false,
                            // Settings
                            pwpLocal: pwpLocal,
                            pwpIsglobal: pwpIsglobal,
                            pwMustChange: pwMustChange,
                            pwTrackUpdate: pwTrackUpdate,
                            pwExpire: pwExpire,
                            pwSendExpire: pwSendExpire,
                            pwLockout: pwLockout,
                            pwUnlock: pwUnlock,
                            pwCheckSyntax: pwCheckSyntax,
                            pwPalindrome: pwPalindrome,
                            pwDictCheck: pwDictCheck,
                            pwAllowHashed: pwAllowHashed,
                            passwordstoragescheme: attrs['passwordstoragescheme'][0],
                            pwInHistory: attrs['passwordinhistory'][0],
                            pwWarning: attrs['passwordwarning'][0],
                            pwMaxAge: attrs['passwordmaxage'][0],
                            pwMinAge: attrs['passwordminage'][0],
                            pwGraceLimit: attrs['passwordgracelimit'][0],
                            pwLockoutDur: attrs['passwordlockoutduration'][0],
                            pwMaxFailure: attrs['passwordmaxfailure'][0],
                            pwResetFailureCount: attrs['passwordresetfailurecount'][0],
                            pwMinLen: attrs['passwordminlength'][0],
                            pwMinDigit: attrs['passwordmindigits'][0],
                            pwMinAlpha: attrs['passwordminalphas'][0],
                            pwMinUppers: attrs['passwordminuppers'][0],
                            pwMinLowers: attrs['passwordminlowers'][0],
                            pwMinSpecial: attrs['passwordminspecials'][0],
                            pwMin8bit: attrs['passwordmin8bit'][0],
                            pwMaxRepeats: attrs['passwordmaxrepeats'][0],
                            pwMaxSeq: attrs['passwordmaxsequence'][0],
                            pwMaxSeqSets: attrs['passwordmaxseqsets'][0],
                            pwMaxClass: attrs['passwordmaxclasschars'][0],
                            pwMinCat: attrs['passwordmincategories'][0],
                            pwMinTokenLen: attrs['passwordmintokenlength'][0],
                            pwBadWords: attrs['passwordbadwords'][0],
                            pwUserAttrs: attrs['passworduserattributes'][0],
                            pwDictPath: attrs['passworddictpath'][0],
                            // Record original values
                            _pwpLocal: pwpLocal,
                            _pwpIsglobal: pwpIsglobal,
                            _pwMustChange: pwMustChange,
                            _pwTrackUpdate: pwTrackUpdate,
                            _pwExpire: pwExpire,
                            _pwSendExpire: pwSendExpire,
                            _pwLockout: pwLockout,
                            _pwUnlock: pwUnlock,
                            _pwCheckSyntax: pwCheckSyntax,
                            _pwPalindrome: pwPalindrome,
                            _pwDictCheck: pwDictCheck,
                            _pwAllowHashed: pwAllowHashed,
                            _passwordstoragescheme: attrs['passwordstoragescheme'][0],
                            _pwWarning: attrs['passwordwarning'][0],
                            _pwInHistory: attrs['passwordinhistory'][0],
                            _pwMaxAge: attrs['passwordmaxage'][0],
                            _pwMinAge: attrs['passwordminage'][0],
                            _pwGraceLimit: attrs['passwordgracelimit'][0],
                            _pwLockoutDur: attrs['passwordlockoutduration'][0],
                            _pwMaxFailure: attrs['passwordmaxfailure'][0],
                            _pwResetFailureCount: attrs['passwordresetfailurecount'][0],
                            _pwMinLen: attrs['passwordminlength'][0],
                            _pwMinDigit: attrs['passwordmindigits'][0],
                            _pwMinAlpha: attrs['passwordminalphas'][0],
                            _pwMinUppers: attrs['passwordminuppers'][0],
                            _pwMinLowers: attrs['passwordminlowers'][0],
                            _pwMinSpecial: attrs['passwordminspecials'][0],
                            _pwMin8bit: attrs['passwordmin8bit'][0],
                            _pwMaxRepeats: attrs['passwordmaxrepeats'][0],
                            _pwMaxSeq: attrs['passwordmaxsequence'][0],
                            _pwMaxSeqSets: attrs['passwordmaxseqsets'][0],
                            _pwMaxClass: attrs['passwordmaxclasschars'][0],
                            _pwMinCat: attrs['passwordmincategories'][0],
                            _pwMinTokenLen: attrs['passwordmintokenlength'][0],
                            _pwBadWords: attrs['passwordbadwords'][0],
                            _pwUserAttrs: attrs['passworduserattributes'][0],
                            _pwDictPath: attrs['passworddictpath'][0],
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                    });
                    this.addNotification(
                        "error",
                        `Error loading global password policy - ${errMsg.desc}`
                    );
                });
    }

    loadLocal(name) {
        // What makes this tough is that local polices can only have a single
        // attribute, while global has them all.  So we have to check every
        // single possible attribute if it's present.
        this.setState({
            loading: true,
        });
        let cmd = [
            "dsconf", "-j", this.props.serverId, "localpwp", "get", name
        ];
        log_cmd("loadLocal", "Load local password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    // Handle the checkbox values
                    let pwMustChange = false;
                    let pwHistory = false;
                    let pwTrackUpdate = false;
                    let pwExpire = false;
                    let pwSendExpire = false;
                    let pwLockout = false;
                    let pwUnlock = false;
                    let pwCheckSyntax = false;
                    let pwPalindrome = false;
                    let pwDictCheck = false;
                    let pwAllowHashed = false;
                    let pwStorageScheme = "PBKDF2_SHA256";
                    let pwInHistory = "";
                    let pwWarning = "";
                    let pwMaxAge = "";
                    let pwMinAge = "";
                    let pwGraceLimit = "";
                    let pwLockoutDur = "";
                    let pwMaxFailure = "";
                    let pwResetFailureCount = "";
                    let pwMinLen = "";
                    let pwMinDigit = "";
                    let pwMinAlpha = "";
                    let pwMinUppers = "";
                    let pwMinLowers = "";
                    let pwMinSpecial = "";
                    let pwMin8bit = "";
                    let pwMaxRepeats = "";
                    let pwMaxSeq = "";
                    let pwMaxSeqSets = "";
                    let pwMaxClass = "";
                    let pwMinCat = "";
                    let pwMinTokenLen = "";
                    let pwBadWords = "";
                    let pwUserAttrs = "";
                    let pwDictPath = "";

                    if ('passwordchange' in attrs && attrs['passwordchange'][0] == "on") {
                        pwMustChange = true;
                    }
                    if ('passwordhistory' in attrs && attrs['passwordhistory'][0] == "on") {
                        pwHistory = true;
                    }
                    if ('passwordtrackupdatetime' in attrs && attrs['passwordtrackupdatetime'][0] == "on") {
                        pwTrackUpdate = true;
                    }
                    if ('passwordsendexpiringtime' in attrs && attrs['passwordsendexpiringtime'][0] == "on") {
                        pwSendExpire = true;
                    }
                    if ('passwordlockout' in attrs && attrs['passwordlockout'][0] == "on") {
                        pwLockout = true;
                    }
                    if ('passwordunlock' in attrs && attrs['passwordunlock'][0] == "on") {
                        pwUnlock = true;
                    }
                    if ('passwordexp' in attrs && attrs['passwordexp'][0] == "on") {
                        pwExpire = true;
                    }
                    if ('passwordchecksyntax' in attrs && attrs['passwordchecksyntax'][0] == "on") {
                        pwCheckSyntax = true;
                    }
                    if ('passwordpalindrome' in attrs && attrs['passwordpalindrome'][0] == "on") {
                        pwPalindrome = true;
                    }
                    if ('passworddictcheck' in attrs && attrs['passworddictcheck'][0] == "on") {
                        pwExpire = true;
                    }
                    if ('passwordstoragescheme' in attrs) {
                        pwStorageScheme = attrs['passwordstoragescheme'][0];
                    }
                    if ('passwordinhistory' in attrs) {
                        pwInHistory = attrs['passwordinhistory'][0];
                    }
                    if ('passwordwarning' in attrs) {
                        pwWarning = attrs['passwordwarning'][0];
                    }
                    if ('passwordmaxage' in attrs) {
                        pwMaxAge = attrs['passwordmaxage'][0];
                    }
                    if ('passwordminage' in attrs) {
                        pwMinAge = attrs['passwordminage'][0];
                    }
                    if ('passwordgracelimit' in attrs) {
                        pwMinAge = attrs['passwordgracelimit'][0];
                    }
                    if ('passwordlockoutduration' in attrs) {
                        pwLockoutDur = attrs['passwordlockoutduration'][0];
                    }
                    if ('passwordmaxfailure' in attrs) {
                        pwMaxFailure = attrs['passwordmaxfailure'][0];
                    }
                    if ('passwordresetfailurecount' in attrs) {
                        pwResetFailureCount = attrs['passwordresetfailurecount'][0];
                    }
                    if ('passwordminlength' in attrs) {
                        pwMinLen = attrs['passwordminlength'][0];
                    }
                    if ('passwordmindigits' in attrs) {
                        pwMinDigit = attrs['passwordmindigits'][0];
                    }
                    if ('passwordminalphas' in attrs) {
                        pwMinAlpha = attrs['passwordminalphas'][0];
                    }
                    if ('passwordminuppers' in attrs) {
                        pwMinLen = attrs['passwordminuppers'][0];
                    }
                    if ('passwordminlowers' in attrs) {
                        pwMinUppers = attrs['passwordminlowers'][0];
                    }
                    if ('passwordminspecials' in attrs) {
                        pwMinSpecial = attrs['passwordminspecials'][0];
                    }
                    if ('passwordmin8bit' in attrs) {
                        pwMin8bit = attrs['passwordmin8bit'][0];
                    }
                    if ('passwordmaxrepeats' in attrs) {
                        pwMaxRepeats = attrs['passwordmaxrepeats'][0];
                    }
                    if ('passwordmaxsequence' in attrs) {
                        pwMaxSeq = attrs['passwordmaxsequence'][0];
                    }
                    if ('passwordmaxseqsets' in attrs) {
                        pwMaxSeqSets = attrs['passwordmaxseqsets'][0];
                    }
                    if ('passwordmaxclasschars' in attrs) {
                        pwMaxClass = attrs['passwordmaxclasschars'][0];
                    }
                    if ('passwordmincategories' in attrs) {
                        pwMinCat = attrs['passwordmincategories'][0];
                    }
                    if ('passwordmintokenlength' in attrs) {
                        pwMinTokenLen = attrs['passwordmintokenlength'][0];
                    }
                    if ('passwordbadwords' in attrs) {
                        pwBadWords = attrs['passwordbadwords'][0];
                    }
                    if ('passworduserattributes' in attrs) {
                        pwUserAttrs = attrs['passworduserattributes'][0];
                    }
                    if ('passworddictpath' in attrs) {
                        pwDictPath = attrs['passworddictpath'][0];
                    }

                    this.setState(() => (
                        {
                            loading: false,
                            // Settings
                            pwLocalMustChange: pwMustChange,
                            pwLocalTrackUpdate: pwTrackUpdate,
                            pwLocalExpire: pwExpire,
                            pwLocalSendExpire: pwSendExpire,
                            pwLocalLockout: pwLockout,
                            pwLocalUnlock: pwUnlock,
                            pwLocalCheckSyntax: pwCheckSyntax,
                            pwLocalPalindrome: pwPalindrome,
                            pwLocalDictCheck: pwDictCheck,
                            pwLocalStorageScheme: pwStorageScheme,
                            pwLocalInHistory: pwInHistory,
                            pwLocalWarning: pwWarning,
                            pwLocalMaxAge: pwMaxAge,
                            pwLocalMinAge: pwMinAge,
                            pwLocalGraceLimit: pwGraceLimit,
                            pwLocalLockoutDur: pwLockoutDur,
                            pwLocalMaxFailure: pwMaxFailure,
                            pwLocalResetFailureCount: pwResetFailureCount,
                            pwLocalMinLen: pwMinLen,
                            pwLocalMinDigit: pwMinDigit,
                            pwLocalMinAlpha: pwMinAlpha,
                            pwLocalMinUppers: pwMinUppers,
                            pwLocalMinLowers: pwMinLowers,
                            pwLocalMinSpecial: pwMinSpecial,
                            pwLocalMin8bit: pwMin8bit,
                            pwLocalMaxRepeats: pwMaxRepeats,
                            pwLocalMaxSeq: pwMaxSeq,
                            pwLocalMaxSeqSets: pwMaxSeqSets,
                            pwLocalMaxClass: pwMaxClass,
                            pwLocalMinCat: pwMinCat,
                            pwLocalMinTokenLen: pwMinTokenLen,
                            pwLocalBadWords: pwBadWords,
                            pwLocalUserAttrs: pwUserAttrs,
                            pwLocalDictPath: pwDictPath,
                            // Record original values
                            _pwLocalMustChange: pwMustChange,
                            _pwLocalTrackUpdate: pwTrackUpdate,
                            _pwLocalExpire: pwExpire,
                            _pwLocalSendExpire: pwSendExpire,
                            _pwLocalLockout: pwLockout,
                            _pwLocalUnlock: pwUnlock,
                            _pwLocalCheckSyntax: pwCheckSyntax,
                            _pwLocalPalindrome: pwPalindrome,
                            _pwLocalDictCheck: pwDictCheck,
                            _pwLocalStorageScheme: pwStorageScheme,
                            _pwLocalInHistory: pwInHistory,
                            _pwLocalWarning: pwWarning,
                            _pwLocalMaxAge: pwMaxAge,
                            _pwLocalMinAge: pwMinAge,
                            _pwLocalGraceLimit: pwGraceLimit,
                            _pwLocalLockoutDur: pwLockoutDur,
                            _pwLocalMaxFailure: pwMaxFailure,
                            _pwLocalResetFailureCount: pwResetFailureCount,
                            _pwLocalMinLen: pwMinLen,
                            _pwLocalMinDigit: pwMinDigit,
                            _pwLocalMinAlpha: pwMinAlpha,
                            _pwLocalMinUppers: pwMinUppers,
                            _pwLocalMinLowers: pwMinLowers,
                            _pwLocalMinSpecial: pwMinSpecial,
                            _pwLocalMin8bit: pwMin8bit,
                            _pwLocalMaxRepeats: pwMaxRepeats,
                            _pwLocalMaxSeq: pwMaxSeq,
                            _pwLocalMaxSeqSets: pwMaxSeqSets,
                            _pwLocalMaxClass: pwMaxClass,
                            _pwLocalMinCat: pwMinCat,
                            _pwLocalMinTokenLen: pwMinTokenLen,
                            _pwLocalBadWords: pwBadWords,
                            _pwLocalUserAttrs: pwUserAttrs,
                            _pwLocalDictPath: pwDictPath,
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                    });
                    this.addNotification(
                        "error",
                        `Error loading local password policy - ${errMsg.desc}`
                    );
                });
    }

    loadSuffixTree() {
        let cmd = [
            "dsconf", "-j", this.props.serverId, "backend", "get-tree",
        ];
        log_cmd("loadSuffixTree", "Start building the suffix tree for local pwp", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let treeData = [];
                    if (content != "") {
                        treeData = JSON.parse(content);
                    }
                    let basicData = [
                        {
                            text: "Global Password Policy",
                            selectable: true,
                            selected: true,
                            icon: "pficon-home",
                            id: "global-policy",

                        },
                        {
                            text: "Suffixes",
                            icon: "pficon-catalog",
                            state: {"expanded": true},
                            selectable: false,
                            id: "pwp-suffixes",
                            nodes: []
                        }
                    ];
                    let current_node = this.state.node_name;
                    basicData[3].nodes = treeData;

                    this.setState(() => ({
                        nodes: basicData,
                        node_name: current_node,
                    }), this.update_tree_nodes);
                });
    }

    selectNode(selectedNode) {
        if (selectedNode.selected) {
            return;
        }
        this.setState({
            disableTree: true // Disable the tree to allow node to be fully loaded
        });

        if (selectedNode.id == GLOBAL_POLICY) {
            // Nothing special to do, this has already been loaded
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                    dbtype: selectedNode.type,
                    bename: "",
                };
            });
        } else {
            if (selectedNode.id in this.state) {
                // This suffix is already cached, just use what we have...
                this.setState(prevState => {
                    return {
                        nodes: this.nodeSelector(prevState.nodes, selectedNode),
                        node_name: selectedNode.id,
                        node_text: selectedNode.text,
                        dbtype: selectedNode.type,
                        bename: selectedNode.be,
                    };
                });
            } else {
                // Load this suffix
                if (selectedNode.type == "suffix" || selecetedNode.type == "subsuffix") {
                    this.loadSuffix(selectedNode.id);
                }
                this.setState(prevState => {
                    return {
                        nodes: this.nodeSelector(prevState.nodes, selectedNode),
                        node_name: selectedNode.id,
                        node_text: selectedNode.text,
                        dbtype: selectedNode.type,
                        bename: selectedNode.be,
                    };
                });
            }
        }
    }

    nodeSelector(nodes, targetNode) {
        return nodes.map(node => {
            if (node.nodes) {
                return {
                    ...node,
                    nodes: this.nodeSelector(node.nodes, targetNode),
                    selected: node.id === targetNode.id ? !node.selected : false
                };
            } else if (node.id === targetNode.id) {
                return { ...node, selected: !node.selected };
            } else if (node.id !== targetNode.id && node.selected) {
                return { ...node, selected: false };
            } else {
                return node;
            }
        });
    }

    addNotification(type, message, timerdelay, persistent) {
        this.setState(prevState => ({
            notifications: [
                ...prevState.notifications,
                {
                    key: prevState.notifications.length + 1,
                    type: type,
                    persistent: persistent,
                    timerdelay: timerdelay,
                    message: message,
                }
            ]
        }));
    }

    removeNotification(notificationToRemove) {
        this.setState({
            notifications: this.state.notifications.filter(
                notification => notificationToRemove.key !== notification.key
            )
        });
    }

    update_tree_nodes() {
        // Set title to the text value of each suffix node.  We need to do this
        // so we can read long suffixes in the UI tree div
        let elements = document.getElementsByClassName('treeitem-row');
        for (let el of elements) {
            el.setAttribute('title', el.innerText);
        }
        this.setState({
            disableTree: false
        });
    }

    render() {
        let pwp_element = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }
        let reloadSpinner = "";
        if (this.state.loading) {
            reloadSpinner = <Spinner loading size="md" />;
        }

        if (this.state.loaded) {
            if (this.state.node_name == GLOBAL_POLICY || this.state.node_name == "") {
                pwp_element =
                    <GlobalPwp
                        serverId={this.props.serverId}
                        addNotification={this.addNotification}
                        reload={this.loadGlobalConfig}
                        data={this.state.globalDBConfig}
                        enableTree={this.enableTree}
                        key={this.state.configUpdated}
                    />;
            } else if (this.state.dbtype == "suffix" || this.state.dbtype == "subsuffix") {
                if (this.state.suffixLoading) {
                    pwp_element =
                        <div className="ds-margin-top ds-loading-spinner ds-center">
                            <h4>Loading password policy for <b>{this.state.node_text} ...</b></h4>
                            <Spinner className="ds-margin-top-lg" loading size="md" />
                        </div>;
                } else {
                    db_element =
                        <Suffix

                        />;
                }
            } else {
                // Chaining
                pwp_element =
                    <div className="ds-margin-top ds-loading-spinner ds-center">
                        <h4>You can not have local password policies on a database link: <b>{this.state.node_text}</b></h4>
                    </div>;
            }
        }

        return (
            <div className="container-fluid">
                <NotificationController
                    notifications={this.state.notifications}
                    removeNotificationAction={this.removeNotification}
                />
                <Row>
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            Password Policy
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh password policies"
                                onClick={() => {
                                    this.loadSuffixTree(1);
                                }}
                            />
                        </ControlLabel>
                        {reloadSpinner}
                    </Col>
                </Row>
                <hr />
                <div className="ds-container">
                    <div className="ds-tree">
                        <div className={disabled} id="pwp-tree"
                            style={treeViewContainerStyles}>
                            <TreeView
                                nodes={nodes}
                                highlightOnHover
                                highlightOnSelect
                                selectNode={this.selectNode}
                            />
                        </div>
                    </div>
                    <div className="ds-tree-content">
                        {pwp_element}
                    </div>
                </div>
            </div>
        );
    }
}
