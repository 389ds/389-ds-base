import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { EncryptedAttrTable } from "./databaseTables.jsx";
import {
	Button,
	Grid,
	GridItem
} from '@patternfly/react-core';
import TypeaheadSelect from "../../dsBasicComponents.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";

const _ = cockpit.gettext;

export class AttrEncryption extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            showConfirmAttrDelete: false,
            addAttr: "",
            delAttr: "",
            modalSpinning: false,
            modalChecked: false,
            saving: false,
        };

        // Delete referral and confirmation
        this.showConfirmAttrDelete = this.showConfirmAttrDelete.bind(this);
        this.closeConfirmAttrDelete = this.closeConfirmAttrDelete.bind(this);
        this.handleAddEncryptedAttr = this.handleAddEncryptedAttr.bind(this);
        this.delEncryptedAttr = this.delEncryptedAttr.bind(this);
        this.onHandleModalChange = this.onHandleModalChange.bind(this);
        // Select Typeahead
        this.handleSelect = this.handleSelect.bind(this);
        this.handleSelectClear = this.handleSelectClear.bind(this);
    }

    onHandleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    showConfirmAttrDelete (name) {
        this.setState({
            showConfirmAttrDelete: true,
            delAttr: name,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmAttrDelete() {
        this.setState({
            showConfirmAttrDelete: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    handleSelect = (event, selection) => {
        this.setState({
            addAttr: selection
        });
    };

    handleSelectClear = () => {
        this.setState({
            addAttr: ""
        });
    };

    handleAddEncryptedAttr () {
        this.setState({
            saving: true
        });

        // Add the new encrypted attr
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "attr-encrypt", "--add-attr=" + this.state.addAttr, this.props.suffix
        ];
        log_cmd("handleAddEncryptedAttr", "Delete suffix referral", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        _("Successfully added encrypted attribute")
                    );
                    this.setState({
                        saving: false,
                        addAttr: "",
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to add encrypted attribute - $0"), errMsg.desc)
                    );
                    this.setState({
                        saving: false,
                        addAttr: "",
                    });
                });
    }

    delEncryptedAttr() {
        this.setState({
            modalSpinning: true
        });
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
                        _("Encrypted attribute successfully deleted")
                    );
                    this.closeConfirmAttrDelete();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure deleting encrypted attribute - $0"), errMsg.desc)
                    );
                    this.closeConfirmAttrDelete();
                });
    }

    render() {
        const {
            addAttr,
            saving,
            modalSpinning,
        } = this.state;

        // Update the available list of attrs for the Typeahead
        const fullList = [];
        const attrs = [];
        // this.props.rows contains strings, not objects
        for (const attrName of this.props.rows) {
            fullList.push(attrName);
        }
        for (const attr of this.props.attrs) {
            if (fullList.indexOf(attr) === -1) {
                attrs.push(attr);
            }
        }

        let saveBtnName = _("Add Attribute");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Adding Attribute ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <div className={saving || modalSpinning ? "ds-margin-top-lg ds-disabled" : "ds-margin-top-lg"}>
                <EncryptedAttrTable
                    key={this.props.rows}
                    rows={this.props.rows}
                    deleteAttr={this.showConfirmAttrDelete}
                />
                <Grid className="ds-margin-top">
                    <GridItem span={6}>
                        <TypeaheadSelect
                            selected={addAttr}
                            onSelect={this.handleSelect}
                            onClear={this.handleSelectClear}
                            options={attrs}
                            placeholder={_("Type attribute name to be encrypted")}
                            noResultsText={_("There are no matching entries")}
                            ariaLabel="Type attribute name to be encrypted"
                            openOnClick={false}
                        />
                    </GridItem>
                    <GridItem span={3} className="ds-no-padding">
                        <Button
                            className="ds-left-margin"
                            variant="primary"
                            onClick={this.handleAddEncryptedAttr}
                            isLoading={saving}
                            isDisabled={addAttr === "" || saving}
                            spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>
                    </GridItem>
                </Grid>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmAttrDelete}
                    closeHandler={this.closeConfirmAttrDelete}
                    handleChange={this.onHandleModalChange}
                    actionHandler={this.delEncryptedAttr}
                    spinning={this.state.modalSpinning}
                    item={this.state.delAttr}
                    checked={this.state.modalChecked}
                    mTitle={_("Remove Attribute Encryption")}
                    mMsg={_("Are you sure you want to remove this encrypted attribute?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
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
    attrs: [],
};
