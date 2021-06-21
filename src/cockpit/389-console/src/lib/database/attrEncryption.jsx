import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { EncryptedAttrTable } from "./databaseTables.jsx";
import {
    Row,
    Col,
} from "patternfly-react";
import {
    Button,
    // Form,
    // FormGroup,
    Select,
    SelectVariant,
    SelectOption,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";

export class AttrEncryption extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            showConfirmAttrDelete: false,
            addAttr: "",
            delAttr: "",
            isSelectOpen: false,
        };

        // Delete referral and confirmation
        this.showConfirmAttrDelete = this.showConfirmAttrDelete.bind(this);
        this.closeConfirmAttrDelete = this.closeConfirmAttrDelete.bind(this);
        this.addEncryptedAttr = this.addEncryptedAttr.bind(this);
        this.delEncryptedAttr = this.delEncryptedAttr.bind(this);
        // Select Typeahead
        this.onSelect = this.onSelect.bind(this);
        this.onSelectToggle = this.onSelectToggle.bind(this);
        this.onSelectClear = this.onSelectClear.bind(this);
    }

    showConfirmAttrDelete (name) {
        this.setState({
            showConfirmAttrDelete: true,
            delAttr: name
        });
    }

    closeConfirmAttrDelete() {
        this.setState({
            showConfirmAttrDelete: false
        });
    }

    onSelect = (event, selection) => {
        if (this.state.addAttr.includes(selection)) {
            this.setState(
                (prevState) => ({
                    addAttr: prevState.addAttr.filter((item) => item !== selection),
                    isSelectOpen: false
                }),
            );
        } else {
            this.setState(
                prevState => ({
                    addAttr: [...prevState.addAttr, selection],
                    isSelectOpen: false
                }),
            );
        }
    }

    onSelectToggle = isSelectOpen => {
        this.setState({
            isSelectOpen
        });
    }

    onSelectClear = () => {
        this.setState({
            addAttr: "",
            isSelectOpen: false
        });
    }

    addEncryptedAttr () {
        if (this.state.addAttr == "") {
            return;
        }

        // Add the new encrypted attr
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "attr-encrypt", "--add-attr=" + this.state.addAttr, this.props.suffix
        ];
        log_cmd("addEncryptedAttr", "Delete suffix referral", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Successfully added encrypted attribute`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failed to delete encrypted attribute - ${errMsg.desc}`
                    );
                });
    }

    delEncryptedAttr() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "attr-encrypt", "--del-attr=" + this.state.delAttr, this.props.suffix
        ];
        log_cmd("delEncryptedAttr", "Delete encrypted attribute", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Encrypted attribute successfully deleted`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure deleting encrypted attribute - ${errMsg.desc}`
                    );
                });
    }

    render() {
        const delAttr = <b>{this.state.delAttr}</b>;

        // Update the available list of attrs for the Typeahead
        let fullList = [];
        let attrs = [];
        for (let attrProp of this.props.rows) {
            fullList.push(attrProp.name);
        }
        for (let attr of this.props.attrs) {
            if (fullList.indexOf(attr) == -1) {
                attrs.push(attr);
            }
        }

        return (
            <div className="ds-margin-top-lg">
                <EncryptedAttrTable
                    key={this.props.rows}
                    rows={this.props.rows}
                    deleteAttr={this.showConfirmAttrDelete}
                />
                <Row className="ds-margin-top">
                    <Col sm={6}>
                        <Select
                            variant={SelectVariant.typeahead}
                            onToggle={this.onSelectToggle}
                            onSelect={this.onSelect}
                            onClear={this.onSelectClear}
                            selections={this.state.addAttr}
                            isOpen={this.state.isSelectOpen}
                            aria-labelledby="typeAhead-AttrEnc"
                            placeholderText="Type attribute name to be encrypted"
                            noResultsFoundText="There are no matching entries"
                            >
                            {attrs.map((attr, index) => (
                                <SelectOption
                                    key={index}
                                    value={attr}
                                />
                            ))}
                        </Select>
                    </Col>
                    <Col sm={3} bsClass="ds-no-padding">
                        <Button
                            variant="primary"
                            onClick={this.addEncryptedAttr}
                        >
                            Add Attribute
                        </Button>
                    </Col>
                </Row>
                <ConfirmPopup
                    showModal={this.state.showConfirmAttrDelete}
                    closeHandler={this.closeConfirmAttrDelete}
                    actionFunc={this.delEncryptedAttr}
                    actionParam={this.state.attrName}
                    msg="Are you sure you want to remove this encrypted attribute?"
                    msgContent={delAttr}
                />
            </div>
        );
    }
}

// Property types and defaults

AttrEncryption.propTypes = {
    rows: PropTypes.array,
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    attrs: PropTypes.array,
    reload: PropTypes.func,
};

AttrEncryption.defaultProps = {
    rows: [],
    suffix: "",
    serverId: "",
    addNotification: noop,
    attrs: [],
    reload: noop,
};
