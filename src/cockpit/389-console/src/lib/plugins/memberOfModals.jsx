import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    Grid,
    GridItem,
    HelperText,
    HelperTextItem,
    Modal,
    ModalVariant,
    Text,
    TextContent,
    TextVariants,
    FormHelperText,
    ValidatedOptions,
    TextInput,
    FormSelect,
    FormSelectOption,
    Tabs,
    Tab,
    TabTitleText,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import { valid_dn } from "../tools.jsx";
import TypeaheadSelect from "../../dsBasicComponents.jsx";
import { MemberOfTable } from "./pluginTables.jsx";
import ExclamationCircleIcon from '@patternfly/react-icons/dist/esm/icons/exclamation-circle-icon';


const _ = cockpit.gettext;

export class MemberOfFixupTaskModal extends React.Component {
    render() {
        const {
            fixupModalShow,
            handleToggleFixupModal,
            handleRunFixup,
            fixupDN,
            fixupFilter,
            handleFieldChange,
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.small}
                aria-labelledby="ds-modal"
                title={_("MemberOf Plugin FixupTask")}
                isOpen={fixupModalShow}
                onClose={handleToggleFixupModal}
                actions={[
                <Button
                    key="confirm"
                        variant="primary"
                        onClick={handleRunFixup}
                        isDisabled={!valid_dn(fixupDN)}
                    >
                        {_("Run")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={handleToggleFixupModal}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <TextContent>
                                <Text className="ds-margin-top" component={TextVariants.h4}>
                                    {_("This task only needs to be run after enabling the plugin for the first time, or if the plugin configuration has changed in a way that will impact the group memberships.")}
                                </Text>
                            </TextContent>
                            <Grid className="ds-margin-top" title={_("Base DN that contains entries to fix up.")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Subtree DN")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={fixupDN}
                                        type="text"
                                        id="fixupDN"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="fixupDN"
                                        onChange={(e, str) => { handleFieldChange(e) }}
                                        validated={!valid_dn(fixupDN) ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                    <FormHelperText>
                                        {_("Value must be a valid DN")}
                                    </FormHelperText>
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-bottom" title={_("Optional. Filter for finding entries to fix up. For example:  (uid=*).  If omitted, all entries with objectclass 'inetuser', 'inetadmin', or 'nsmemberof' under the specified subtree DN will have their memberOf attribute regenerated.")}>
                                <GridItem span={3} className="ds-label">
                                    {_("Search Filter")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={fixupFilter}
                                        type="text"
                                        id="fixupFilter"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="fixupFilter"
                                        onChange={(e, str) => { handleFieldChange(e) }}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                    </GridItem>
                </Grid>
            </Modal>
        );
    }
}

export class MemberOfConfigEntryModal extends React.Component {
    render() {
        const {
            configEntryModalShow,
            activeTabModalKey,
            handleModalChange,
            objectClasses,
            newEntry,
            settings,
            errorModal,
            handlers,
            openers,
            saveBtnDisabledModal,
            savingModal,
            extraPrimaryProps,
            validateModalFilterCreate,
            openSpecificExcludeGroupAddModal,
        } = this.props;

        let modalButtons = [];
        if (!newEntry) {
            modalButtons = [
                <Button key="del" variant="primary" onClick={this.handleShowConfirmDelete}>
                    {_("Delete Config")}
                </Button>,
                <Button
                    key="save"
                    variant="primary"
                    onClick={handlers.handleEditConfig}
                    isDisabled={saveBtnDisabledModal || savingModal}
                    isLoading={savingModal}
                    spinnerAriaValueText={savingModal ? _("Saving") : undefined}
                    {...extraPrimaryProps}
                >
                    {savingModal ? _("Saving ...") : _("Save Config")}
                </Button>,
                <Button key="cancel" variant="link" onClick={handlers.handleCloseModal}>
                    {_("Cancel")}
                </Button>
            ];
        } else {
            modalButtons = [
                <Button
                    key="add"
                    variant="primary"
                    onClick={handlers.handleAddConfig}
                    isDisabled={saveBtnDisabledModal || savingModal}
                    isLoading={savingModal}
                    spinnerAriaValueText={savingModal ? _("Saving") : undefined}
                    {...extraPrimaryProps}
                >
                    {savingModal ? _("Adding ...") : _("Add Config")}
                </Button>,
                <Button key="cancel" variant="link" onClick={handlers.handleCloseModal}>
                    {_("Cancel")}
                </Button>
            ];
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={_("Manage MemberOf Plugin Shared Config Entry")}
                isOpen={configEntryModalShow}
                onClose={handlers.handleCloseModal}
                actions={modalButtons}
            >
                <Tabs isFilled activeKey={activeTabModalKey} onSelect={handlers.handleNavSelectModal}>
                    <Tab eventKey={0} title={<TabTitleText>{_("Plugin Settings")}</TabTitleText>}>
                        <Form isHorizontal autoComplete="off">
                            <Grid className="ds-margin-top-lg" title={_("The config entry full DN")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Config DN")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={settings.configDN}
                                        type="text"
                                        id="configDN"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="configDN"
                                        onChange={(e, str) => { handlers.handleModalChange(e) }}
                                        validated={errorModal.configDN ? ValidatedOptions.error : ValidatedOptions.default}
                                        isDisabled={!newEntry}
                                    />
                                    {newEntry &&
                                        <FormHelperText>
                                            {_("Value must be a valid DN")}
                                        </FormHelperText>
                                    }
                                </GridItem>
                            </Grid>
                            <Grid title={_("Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Membership Attribute")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TypeaheadSelect
                                        selected={settings.configAttr}
                                        onSelect={handlers.handleConfigAttrSelect}
                                        onClear={handlers.handleConfigAttrClear}
                                        options={["memberOf"]}
                                        placeholder={_("Type a member attribute...")}
                                        noResultsText={_("There are no matching entries")}
                                        validated={errorModal.configAttr ? "error" : "default"}
                                        ariaLabel="Type a member attribute"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-top" title={_("Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Group Attribute")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TypeaheadSelect
                                        isMulti
                                        hasCheckbox
                                        selected={settings.configGroupAttr}
                                        onSelect={handlers.handleConfigGroupAttrSelect}
                                        onClear={handlers.handleConfigGroupAttrClear}
                                        options={["member", "memberCertificate", "uniqueMember"]}
                                        placeholder={_("Type a member group attribute...")}
                                        noResultsText={_("There are no matching entries")}
                                        validated={errorModal.configGroupAttr ? "error" : "default"}
                                        ariaLabel="Type a member group attribute"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Auto Add OC")}
                                </GridItem>
                                <GridItem span={9}>
                                    <FormSelect
                                        id="configAutoAddOC"
                                        value={settings.configAutoAddOC}
                                        onChange={(event, value) => {
                                            handlers.handleModalChange(event);
                                        }}
                                        aria-label="FormSelect Input"
                                    >
                                        <FormSelectOption key="no_setting2" value="" label="-" />
                                        {objectClasses.map((attr, index) => (
                                            <FormSelectOption key={attr} value={attr} label={attr} />
                                        ))}
                                    </FormSelect>
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-left-margin" span={3}>
                                    <Checkbox
                                        id="configSkipNested"
                                        isChecked={settings.configSkipNested}
                                        onChange={(e, checked) => { handleModalChange(e) }}
                                        title={_("Specifies wherher to skip nested groups or not (memberOfSkipNested)")}
                                        label={_("Skip Nested")}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)")}>
                                <GridItem className="ds-left-margin" span={3}>
                                    <Checkbox
                                        id="configAllBackends"
                                        isChecked={settings.configAllBackends}
                                        onChange={(e, checked) => { handlers.handleModalChange(e) }}
                                        label={_("All Backends")}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Subtree Scopes")}</TabTitleText>}>
                        <Form isHorizontal autoComplete="off">
                            <Grid className="ds-margin-top" title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Subtree Scope")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TypeaheadSelect
                                        isMulti
                                        selected={settings.configEntryScope}
                                        onSelect={handlers.handleConfigScopeSelect}
                                        onClear={handlers.handleConfigScopeClear}
                                        options={settings.configEntryScopeOptions}
                                        isCreatable
                                        onCreateOption={handlers.handleConfigCreateOption}
                                        validateCreate={(value) => valid_dn(value)}
                                        placeholder={_("Type a subtree DN...")}
                                        noResultsText={_("There are no matching entries")}
                                        validated={errorModal.configEntryScope ? "error" : "default"}
                                        onToggle={handlers.handleConfigScopeToggle}
                                        isOpen={openers.isConfigSubtreeScopeOpen}
                                        ariaLabel="Type a subtree DN"
                                    />
                                    <FormHelperText>
                                        {_("Values must be valid DN's")}
                                    </FormHelperText>
                                </GridItem>
                            </Grid>
                            <Grid title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Exclude Subtree")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TypeaheadSelect
                                        isMulti
                                        selected={settings.configEntryScopeExcludeSubtreeScope}
                                        onSelect={handlers.handleConfigExcludeScopeSelect}
                                        onClear={handlers.handleConfigExcludeScopeClear}
                                        options={settings.configEntryScopeExcludeOptions}
                                        isCreatable
                                        onCreateOption={handlers.handleConfigExcludeCreateOption}
                                        validateCreate={(value) => valid_dn(value)}
                                        placeholder={_("Type a subtree DN...")}
                                        noResultsText={_("There are no matching entries")}
                                        validated={errorModal.configEntryScopeExcludeSubtreeScope ? "error" : "default"}
                                        ariaLabel="Type a subtree DN"
                                        onToggle={handlers.handleConfigExcludeScopeToggle}
                                        isOpen={openers.isConfigExcludeScopeOpen}
                                    />
                                    <FormHelperText>
                                        {_("Values must be valid DN's")}
                                    </FormHelperText>
                                </GridItem>
                            </Grid>
                        </Form>
                    </Tab>

                    {!newEntry && (
                        <Tab eventKey={2} title={<TabTitleText>{_("Group Scopes")}</TabTitleText>}>
                            <MemberOfTable
                                title={"Specific Group Filters"}
                                rows={settings.configSpecificGroup}
                                addAttr={handlers.openSpecificGroupAddModal}
                                deleteAttr={handlers.openDeleteFilterConfirmation}
                            />
                            <Button
                                id="specific-group-filter"
                                className="ds-margin-top"
                                key="specific-group-filter"
                                variant="secondary"
                                onClick={handlers.openSpecificGroupAddModal}
                            >
                                Add filter
                            </Button>
                            <MemberOfTable
                                title={"Specific Group Exclude Filters"}
                                rows={settings.configExcludeSpecificGroup}
                                addAttr={handlers.openSpecificExcludeGroupAddModal}
                                deleteAttr={handlers.openDeleteExcludeFilterConfirmation}
                            />
                            <Button
                                id="specific-group-exclude-filter"
                                className="ds-margin-top"
                                key="specific-group-exclude-filter"
                                variant="secondary"
                                onClick={openSpecificExcludeGroupAddModal}
                            >
                                Add exclude filter
                            </Button>
                            <Grid
                                className="ds-margin-top-lg"
                                title={_("Specifies the objectclasses for the specific groups to include/exclude (memberOfSpecificGroupOC)")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Specific Group OC")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TypeaheadSelect
                                        isMulti
                                        selected={settings.configSpecificGroupOC}
                                        onSelect={handlers.handleConfigSpecificGroupOCSelect}
                                        onClear={handlers.handleConfigSpecificGroupOCClear}
                                        options={objectClasses}
                                        placeholder={_("Type a objectclass...")}
                                        noResultsText="There are no matching objectclasses"
                                        ariaLabel="Type a objectclass"
                                        onToggle={handlers.handleConfigSpecificGroupOCToggle}
                                        isOpen={openers.isConfigSpecificGroupOCOpen}
                                    />
                                </GridItem>
                            </Grid>
                        </Tab>
                    )}
                    {newEntry && (
                            <Tab eventKey={2} title={<TabTitleText>{_("Specific Group Filters")}</TabTitleText>}>
                                <Grid
                                    className="ds-margin-top-lg"
                                    title={"Specifies a filter for specific groups to include, example: (cn=group). (memberOfSpecificGroupFilter)"}
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {"Specific Group Filter"}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            isMulti
                                            selected={settings.configSpecificGroup}
                                            onSelect={handlers.handleConfigSpecificGroupSelect}
                                            onClear={handlers.handleConfigSpecificGroupClear}
                                            options={settings.configSpecificGroupOptions}
                                            isCreatable
                                            onCreateOption={handlers.handleConfigSpecificGroupCreateOption}
                                            validateCreate={(value) => validateModalFilterCreate(value, 'configSpecificGroup')}
                                            placeholder={_("Type a search filter...")}
                                            validated={errorModal.configSpecificGroup ? "error" : "default"}
                                            ariaLabel="Type a search filter"
                                            onToggle={handlers.handleConfigSpecificGroupToggle}
                                            isOpen={openers.isConfigSpecificGroupOpen}
                                        />
                                        <FormHelperText>
                                            {_("Values must be valid search filters")}
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid
                                    className="ds-margin-top-lg"
                                    title={"Specifies a filter for specific groups to exclude, example: (cn=group). (memberOfExcludeSpecificGroupFilter)"}
                                >
                                <GridItem className="ds-label" span={3}>
                                    {"Exclude Specific Group Filter"}
                                </GridItem>
                                <GridItem span={9}>
                                    <TypeaheadSelect
                                        isMulti
                                        selected={settings.configExcludeSpecificGroup}
                                        onSelect={handlers.handleConfigExcludeSpecificGroupSelect}
                                        onClear={handlers.handleConfigExcludeSpecificGroupClear}
                                        options={settings.configExcludeSpecificGroupOptions}
                                        isCreatable
                                        onCreateOption={handlers.handleConfigExcludeSpecificGroupCreateOption}
                                        validateCreate={(value) => validateModalFilterCreate(value, 'configExcludeSpecificGroup')}
                                        placeholder={_("Type a search filter...")}
                                        validated={errorModal.configExcludeSpecificGroup ? "error" : "default"}
                                        ariaLabel="Type a search filter"
                                        onToggle={handlers.handleConfigExcludeSpecificGroupToggle}
                                        isOpen={openers.isConfigExcludeSpecificGroupOpen}
                                    />
                                        <FormHelperText>
                                            {"Values must be valid search filters"}
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("Specifies the objectclasses for the specific groups to include/exclude (memberOfSpecificGroupOC)")}
                                    className="ds-margin-top-lg"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Specific Group OC")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            isMulti
                                            selected={settings.configSpecificGroupOC}
                                            onSelect={handlers.handleConfigSpecificGroupOCSelect}
                                            onClear={handlers.handleConfigSpecificGroupOCClear}
                                            options={objectClasses}
                                            placeholder={_("Type a objectclass...")}
                                            noResultsText="There are no matching objectclasses"
                                            ariaLabel="Type a objectclass"
                                            onToggle={handlers.handleConfigSpecificGroupOCToggle}
                                            isOpen={openers.isConfigSpecificGroupOCOpen}
                                        />
                                        <FormHelperText>
                                            {_("Values must be valid objectclasses")}
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                            </Tab>
                        )}
                </Tabs>
            </Modal>
        );
    }
}

MemberOfConfigEntryModal.propTypes = {
    handlers: PropTypes.object,
    openers: PropTypes.object,
    settings: PropTypes.object,
    errorModal: PropTypes.object,
    activeTabModalKey: PropTypes.number,
    objectClasses: PropTypes.array,
    newEntry: PropTypes.bool,
    configEntryModalShow: PropTypes.bool,
    saveBtnDisabledModal: PropTypes.bool,
    savingModal: PropTypes.bool,
    extraPrimaryProps: PropTypes.object,
    validateModalFilterCreate: PropTypes.func,
};

MemberOfConfigEntryModal.defaultProps = {
    handlers: {},
    openers: {},
    settings: {},
    errorModal: {},
    activeTabModalKey: 0,
    objectClasses: [],
    newEntry: false,
    configEntryModalShow: false,
    saveBtnDisabledModal: false,
    savingModal: false,
    extraPrimaryProps: {},
    validateModalFilterCreate: () => {},
};

export class MemberOfSpecificGroupFilterModal extends React.Component {
    render() {
        const {
            isSpecificGroupModalOpen,
            closeSpecificGroupAddModal,
            handleAddDelSpecificGroupFilter,
            groupFilter,
            groupFilterType,
            handleFieldChange,
            modalSpinning,
            validFilterChange,
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.small}
                aria-labelledby="ds-modal"
                title={groupFilterType === "include" ? _("Add Specific Group Filter") : _("Add Specific Group Exclude Filter")}
                isOpen={isSpecificGroupModalOpen}
                onClose={closeSpecificGroupAddModal}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={() => handleAddDelSpecificGroupFilter("add", groupFilter)}
                        isDisabled={!validFilterChange(groupFilter)}
                    >
                        {modalSpinning ? _("Adding ...") : _("Add Filter")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeSpecificGroupAddModal}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text className="ds-margin-top" component={TextVariants.h4}>
                            {_("Enter an LDAP search filter, example: (cn=group)")}
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            {_(groupFilterType === "include" ? "Specific Group Filter" : "Specific Group Exclude Filter")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={groupFilter}
                                type="text"
                                id="groupFilter"
                                aria-describedby="horizontal-form-name-helper"
                                name="groupFilter"
                                onChange={(e, str) => { handleFieldChange(e) }}
                                validated={groupFilter !== "" && !validFilterChange(groupFilter) ? ValidatedOptions.error : ValidatedOptions.default}
                                isDisabled={modalSpinning}
                            />
                            <FormHelperText>
                                <HelperText>
                                    <HelperTextItem variant={groupFilter !== "" && !validFilterChange(groupFilter) ? 'error' : 'default'} {...(groupFilter !== "" && !validFilterChange(groupFilter) && { icon: <ExclamationCircleIcon /> })}>
                                        {_("Value must be a valid LDAP filter, and it must be unique.")}
                                    </HelperTextItem>
                                </HelperText>
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}
