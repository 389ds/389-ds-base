import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { EncryptedAttrTable } from "./databaseTables.jsx";
import {
    Row,
    Col,
    Button,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";
import { Typeahead } from "react-bootstrap-typeahead";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

export class AttrEncryption extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            showConfirmAttrDelete: false,
            typeahead: "",
            addAttr: "",
            delAttr: "",
        };

        // Delete referral and confirmation
        this.showConfirmAttrDelete = this.showConfirmAttrDelete.bind(this);
        this.closeConfirmAttrDelete = this.closeConfirmAttrDelete.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.addEncryptedAttr = this.addEncryptedAttr.bind(this);
        this.delEncryptedAttr = this.delEncryptedAttr.bind(this);
    }

    showConfirmAttrDelete (item) {
        this.setState({
            showConfirmAttrDelete: true,
            delAttr: item.name
        });
    }

    closeConfirmAttrDelete() {
        this.setState({
            showConfirmAttrDelete: false
        });
    }

    handleTypeaheadChange (value) {
        this.setState({
            addAttr: value
        });
    }

    addEncryptedAttr () {
        // reset typeahead input field
        this.typeahead.getInstance().clear();
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
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failed to delete encrypted attribute - ${err}`
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
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure deleting encrypted attribute - ${err}`
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
            <div>
                <EncryptedAttrTable
                    rows={this.props.rows}
                    loadModalHandler={this.showConfirmAttrDelete}
                />
                <p />
                <Row>
                    <Col sm={6}>
                        <Typeahead
                            id="attrEncrypt"
                            onChange={value => {
                                this.handleTypeaheadChange(value);
                            }}
                            maxResults={1000}
                            options={attrs}
                            placeholder="Type attribute name to be encrypted"
                            ref={(typeahead) => { this.typeahead = typeahead }}
                        />
                    </Col>
                    <Col sm={3} bsClass="ds-no-padding">
                        <Button
                            bsStyle="primary"
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
