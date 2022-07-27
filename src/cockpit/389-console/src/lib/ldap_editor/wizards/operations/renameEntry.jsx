import React from 'react';
import {
    Alert,
    Button,
    Card,
    CardBody,
    CardTitle,
    Checkbox,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Label,
    LabelGroup,
    SimpleList,
    SimpleListItem,
    Spinner,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    ValidatedOptions,
    Wizard
} from '@patternfly/react-core';
import LdapNavigator from '../../lib/ldapNavigator.jsx';
import {
    getBaseLevelEntryAttributes,
    getRdnInfo,
    modifyLdapEntry
} from '../../lib/utils.jsx';

class RenameEntry extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            stepIdReached: 1,
            baseDn: "",
            isTreeLoading: false,
            searching: false,
            renaming: true,
            currRdnAttr: "",
            currRdnVal: "",
            currRdnSuffix: "",
            deleteOldRdn: false,
            newRdnAttr: "",
            newRdnVal: "",
            newRdnSuffix: "",
            nextEnabled: false,
            allowedAttrs: [],
            ldifArray: [],
            resultVariant: "success",
            commandOutput: "",
        };

        this.onNext = ({ id }) => {
            this.setState({
                stepIdReached: this.state.stepIdReached < id ? id : this.state.stepIdReached
            });

            if (id === 4) {
                // Generate LDIF changes to review
                this.generateLdifData();
            } else if (id === 5) {
                // Do the modrdn
                const params = { serverId: this.props.editorLdapServer };
                modifyLdapEntry(params, this.state.ldifArray, (result) => {
                    const opInfo = {
                        operationType: 'MODRDN',
                        resultCode: result.errorCode,
                        time: Date.now(),
                    }
                    this.props.setWizardOperationInfo(opInfo);
                    this.props.onModrdnReload();
                    if (result.errorCode === 0) {
                        result.output = "Successfully renamed entry"
                    }
                    this.setState({
                        commandOutput: result.errorCode === 0 ? 'Successfully renamed entry!' : 'Failed to rename entry, error: ' + result.errorCode ,
                        resultVariant: result.errorCode === 0 ? 'success' : 'danger',
                        renaming: false
                    });
                });
            }
        };

        this.handleBaseDnSelection = (treeViewItem) => {
            if (treeViewItem.dn.toLowerCase().includes(this.props.wizardEntryDn.toLowerCase())) {
                this.props.addNotification("warning",
                    "Can not select a subtree that is the current entry, or a child of the current entry.");
                return;
            }
            this.setState({
                newRdnSuffix: treeViewItem.dn
            });
        }

        this.showTreeLoadingState = (isTreeLoading) => {
            this.setState({
                isTreeLoading,
                searching: isTreeLoading ? true : false
            });
        }

        this.handleRdnAttrChange = (attr) => {
            this.setState({
                newRdnAttr: attr
            });
        };

        this.handleRdnValChange = (val) => {
            this.setState({
                newRdnVal: val
            });
        };

        this.handleDelRdnChange = (checked) => {
            this.setState({
                deleteOldRdn: checked
            });
        }

        this.generateLdifData = this.generateLdifData.bind(this);
    }

    componentDidMount () {
        getBaseLevelEntryAttributes(this.props.editorLdapServer,
            this.props.wizardEntryDn,
            (entryDetails) => {
                let objectclasses = [];
                let allowedAttrs = [];
                const rdnInfo = getRdnInfo(this.props.wizardEntryDn);

                entryDetails
                .filter(data => (data.attribute + data.value !== '' && // Filter out empty lines
                data.attribute !== '???: ')) // and data for empty suffix(es) and in case of failure.
                .map((line, index) => {
                    const attr = line.attribute;
                    const attrLowerCase = attr.trim().toLowerCase();
                    let val = line.value.substring(1).trim().toLowerCase();
                    if (attrLowerCase === "objectclass") {
                        objectclasses.push(val);
                    }
                });

                // Gather all the allowed attributes for the rdn attr
                for (const entryOC of objectclasses) {
                    for (const oc of this.props.allObjectclasses) {
                        if (oc.name.toLowerCase() === entryOC) {
                            for (const attr of oc.required) {
                                if (allowedAttrs.indexOf(attr) === -1) {
                                    allowedAttrs.push(attr);
                                }
                            }
                            for (const attr of oc.optional) {
                                if (allowedAttrs.indexOf(attr) === -1) {
                                    allowedAttrs.push(attr);
                                }
                            }
                        }
                    }
                }

                const rdnSuffix = this.props.wizardEntryDn.replace(rdnInfo.rdnAttr + "=" + rdnInfo.rdnVal + ",", "").trim();
                this.setState({
                    allowedAttrs,
                    currRdnAttr: rdnInfo.rdnAttr,
                    currRdnVal: rdnInfo.rdnVal,
                    currRdnSuffix: rdnSuffix,
                    newRdnAttr: rdnInfo.rdnAttr,
                    newRdnVal: rdnInfo.rdnVal,
                    newRdnSuffix: rdnSuffix,
                    baseDn: rdnSuffix,
                });
        });
    }

    generateLdifData () {
        let ldifArray = [];

        ldifArray.push(`dn: ${this.props.wizardEntryDn}`); // DN line.
        ldifArray.push('changetype: modrdn');
        ldifArray.push(`newrdn: ${this.state.newRdnAttr}=${this.state.newRdnVal}`);
        if (this.state.deleteOldRdn) {
            ldifArray.push(`deleteoldrdn: 1`);
        } else {
            ldifArray.push(`deleteoldrdn: 0`);
        }
        if (this.state.newRdnSuffix !== this.state.currRdnSuffix) {
            ldifArray.push(`newsuperior: ${this.state.newRdnSuffix}`);
        }
        this.setState({
            ldifArray
        })
    }

    render () {
        const {
            allowedAttrs, stepIdReached, nextEnabled, newRdnAttr, newRdnVal,
            newRdnSuffix, ldifArray, resultVariant, commandOutput, currRdnAttr,
            currRdnVal, currRdnSuffix
        } = this.state;

        const attrValStep =
            <>
                <TextContent>
                    <Text component={TextVariants.h2}>
                        Select The Naming Attribute And Value
                    </Text>
                </TextContent>
                <Grid className="ds-margin-top-lg">
                    <GridItem span={3} className="ds-label">
                        Naming Attribute
                    </GridItem>
                    <GridItem span={9}>
                        <FormSelect
                            id="naming-attr"
                            value={newRdnAttr}
                            onChange={(str, e) => {
                                this.handleRdnAttrChange(str);
                            }}
                            aria-label="FormSelect Input"
                        >
                            {allowedAttrs.map((attr, index) => (
                                <FormSelectOption key={attr} value={attr} label={attr} />
                            ))}
                        </FormSelect>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem span={3} className="ds-label">
                        New Name
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={newRdnVal}
                            type="text"
                            id="naming-val"
                            aria-describedby="horizontal-form-name-helper"
                            name="rdnVal"
                            onChange={(str, e) => {
                                this.handleRdnValChange(str);
                            }}
                            validated={newRdnVal === "" ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </GridItem>
                    <div title="Removes the old RDN attribute from the entry, otherwise this attribute remains in the entry and references the old RDN.">
                        <Checkbox
                            className="ds-margin-top-lg"
                            id="deleteoldrdn"
                            isChecked={this.state.deleteOldRdn}
                            onChange={this.handleDelRdnChange}
                            label="Delete the old RDN attribute from the entry"
                        />
                    </div>
                </Grid>
            </>;

        const locationStep =
            <>
                <TextContent>
                    <Text component={TextVariants.h2}>
                        Select The Entry Location
                    </Text>
                    <Text component={TextVariants.h5} className="ds-indent ds-margin-top-lg" title="The superior DN where the entry will be located under.">
                        Parent Subtree &nbsp;&nbsp;
                        <Label color="blue">
                            {newRdnSuffix}
                        </Label>
                    </Text>
                </TextContent>
                <CardBody className="ds-margin-top">
                    <LdapNavigator
                        treeItems={[...this.props.treeViewRootSuffixes]}
                        editorLdapServer={this.props.editorLdapServer}
                        handleNodeOnClick={this.handleBaseDnSelection}
                        showTreeLoadingState={this.showTreeLoadingState}
                    />
                </CardBody>
            </>;

        // Build the New RDN Labels
        const intactRdn = newRdnAttr + "=" + newRdnVal;
        const rdnAttrLabel = newRdnAttr !== currRdnAttr || newRdnVal !== currRdnVal ?
                <Label color="blue">
                    {newRdnAttr}={newRdnVal}
                </Label>
            : <div className="ds-lower-field">{intactRdn}</div>

        const rdnSuffixLabel = newRdnSuffix !== currRdnSuffix  ?
                <Label color="blue">
                    {newRdnSuffix}
                </Label>
            : <div className="ds-lower-field">{newRdnSuffix}</div>

        // Build the Current RDN labels
        const intactCurrRdn = currRdnAttr + "=" + currRdnVal;
        const rdnCurrAttrLabel = currRdnAttr !== newRdnAttr || newRdnVal !== currRdnVal ?
                <Label variant="outline" color="grey">
                    {currRdnAttr}={currRdnVal}
                </Label>
            : <div className="ds-lower-field">{intactCurrRdn}</div>

        const rdnCurrSuffixLabel = newRdnSuffix !== currRdnSuffix  ?
                <Label variant="outline" color="grey">
                    {currRdnSuffix}
                </Label>
            : <div className="ds-lower-field">{currRdnSuffix}</div>

        const reviewChangesStep =
            <>
                <TextContent>
                    <Text component={TextVariants.h2}>
                        Review Changes
                    </Text>
                </TextContent>
                <Grid className="ds-margin-top-lg">
                    <GridItem span={2} className="ds-label">
                        Original DN
                    </GridItem>
                    <GridItem span={10}>
                        <LabelGroup>
                            {rdnCurrAttrLabel},
                            {rdnCurrSuffixLabel}
                        </LabelGroup>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem span={2} className="ds-label">
                        New DN
                    </GridItem>
                    <GridItem span={10}>
                        <LabelGroup>
                            {rdnAttrLabel},
                            {rdnSuffixLabel}
                        </LabelGroup>
                    </GridItem>
                </Grid>
                <div className={!this.state.deleteOldRdn ? "ds-hidden" : "ds-margin-top-lg"}>
                    <Grid title="This attribute will be removed from the entry so that there is no trace of the old RDN in the entry.">
                        <GridItem span={2} className="ds-label">
                            Remove Old Rdn
                        </GridItem>
                        <GridItem span={10}>
                            <Label color="orange">
                                {currRdnAttr}: {currRdnVal}
                            </Label>
                        </GridItem>
                    </Grid>
                </div>
            </>;

        const ldifListItems = ldifArray.map((line, index) =>
            <SimpleListItem key={index} isCurrent={line.startsWith('dn: ')}>
                {line}
            </SimpleListItem>
        );

        const ldifStatementsStep =
            <>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant="info"
                        isInline
                        title="LDIF Statements"
                    />
                </div>
                <Card isSelectable>
                    <CardBody>
                        { (ldifListItems.length > 0) &&
                            <SimpleList aria-label="LDIF data modrdn">
                                {ldifListItems}
                            </SimpleList>
                        }
                    </CardBody>
                </Card>
            </>;

        let nb = -1;
        const ldifLines = ldifArray.map(line => {
            nb++;
            return { data: line, id: nb };
        });

        const reviewStep = (
            <div>
                <div className="ds-addons-bottom-margin">
                    <Alert
                        variant={resultVariant}
                        isInline
                        title="Result for Entry Modification"
                    >
                        {commandOutput}
                        {this.state.renaming &&
                            <div>
                                <Spinner className="ds-left-margin" size="md" />
                                &nbsp;&nbsp;Renaming entry ...
                            </div>
                        }
                    </Alert>
                </div>
                {resultVariant === 'danger' &&
                    <Card isSelectable>
                        <CardTitle>LDIF Data</CardTitle>
                        <CardBody>
                            {ldifLines.map((line) => (
                                <h6 key={line.id}>{line.data}</h6>
                            ))}
                        </CardBody>
                    </Card>
                }
            </div>
        );


        const renameSteps = [
            {
                id: 1,
                name: 'Set Naming Attribute',
                component: attrValStep,
                canJumpTo: stepIdReached < 5,
                hideBackButton: true,
                enableNext: newRdnVal !== "",
            },
            {
                id: 2,
                name: 'Set Entry Location',
                component: locationStep,
                canJumpTo: stepIdReached < 5 && newRdnVal !== "",
                enableNext: newRdnVal !== "" && ((newRdnVal !== currRdnVal || newRdnAttr !== currRdnAttr) || (newRdnSuffix !== currRdnSuffix)),
            },
            {
                id: 3,
                name: 'Review Changes',
                component: reviewChangesStep,
                canJumpTo: stepIdReached >= 3 && stepIdReached < 5,
            },
            {
                id: 4,
                name: 'LDIF Statements',
                component: ldifStatementsStep,
                nextButtonText: 'Change Entry Name',
                canJumpTo: stepIdReached >+ 4 && stepIdReached < 5,
            },
            {
                id: 5,
                name: 'Review Result',
                component: reviewStep,
                nextButtonText: 'Finish',
                canJumpTo: stepIdReached > 5,
                hideBackButton: true,
                enableNext: !this.state.renaming
            }
        ];

        const desc =
            <>
                Old DN: &nbsp;&nbsp;&nbsp;<b>{currRdnAttr}={currRdnVal},{currRdnSuffix}</b>
                <br /><br />
                New DN: &nbsp;&nbsp;<b>{newRdnAttr}={newRdnVal},{newRdnSuffix}</b>
            </>;

        return (
            <Wizard
                isOpen={this.props.isWizardOpen}
                onClose={this.props.toggleOpenWizard}
                onNext={this.onNext}
                title="Rename LDAP Entry"
                description={desc}
                steps={renameSteps}
            />
        );
    }
}

export default RenameEntry;
