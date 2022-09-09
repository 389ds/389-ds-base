import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { EncryptedAttrTable } from "./databaseTables.jsx";
import {
    Button,
    Grid,
    GridItem,
    Select,
    SelectVariant,
    SelectOption,
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
            modalSpinning: false,
            modalChecked: false,
            saving: false,
        };

        // Delete referral and confirmation
        this.showConfirmAttrDelete = this.showConfirmAttrDelete.bind(this);
        this.closeConfirmAttrDelete = this.closeConfirmAttrDelete.bind(this);
        this.addEncryptedAttr = this.addEncryptedAttr.bind(this);
        this.delEncryptedAttr = this.delEncryptedAttr.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        // Select Typeahead
        this.onSelect = this.onSelect.bind(this);
        this.onSelectToggle = this.onSelectToggle.bind(this);
        this.onSelectClear = this.onSelectClear.bind(this);
    }

    handleModalChange(e) {
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

    onSelect = (event, selection) => {
        this.setState({
            addAttr: selection,
            isSelectOpen: false
        });
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
        this.setState({
            saving: true
        });

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
                        `Failed to add encrypted attribute - ${errMsg.desc}`
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
                        `Encrypted attribute successfully deleted`
                    );
                    this.closeConfirmAttrDelete();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure deleting encrypted attribute - ${errMsg.desc}`
                    );
                    this.closeConfirmAttrDelete();
                });
    }

    render() {
        const {
            addAttr,
            saving,
            isSelectOpen,
            modalSpinning,
        } = this.state;

        // Update the available list of attrs for the Typeahead
        const fullList = [];
        const attrs = [];
        for (const attrProp of this.props.rows) {
            fullList.push(attrProp.name);
        }
        for (const attr of this.props.attrs) {
            if (fullList.indexOf(attr) == -1) {
                attrs.push(attr);
            }
        }

        let saveBtnName = "Add Attribute";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Adding Attribute ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
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
                        <Select
                            variant={SelectVariant.typeahead}
                            onToggle={this.onSelectToggle}
                            onSelect={this.onSelect}
                            onClear={this.onSelectClear}
                            selections={addAttr}
                            isOpen={isSelectOpen}
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
                    </GridItem>
                    <GridItem span={3} className="ds-no-padding">
                        <Button
                            className="ds-left-margin"
                            variant="primary"
                            onClick={this.addEncryptedAttr}
                            isLoading={saving}
                            isDisabled={addAttr == "" || saving}
                            spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>
                    </GridItem>
                </Grid>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmAttrDelete}
                    closeHandler={this.closeConfirmAttrDelete}
                    handleChange={this.handleModalChange}
                    actionHandler={this.delEncryptedAttr}
                    spinning={this.state.modalSpinning}
                    item={this.state.delAttr}
                    checked={this.state.modalChecked}
                    mTitle="Remove Attribute Encryption"
                    mMsg="Are you sure you want to remove this encrypted attribute?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
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
