import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Alert,
    Button,
    DescriptionList,
    DescriptionListDescription,
    DescriptionListGroup,
    DescriptionListTerm,
    Divider,
    Modal,
    ModalVariant,
    Spinner,
} from "@patternfly/react-core";
import {
    fetchUserEffectivePasswordPolicy,
    getBaseDnForEntry,
    getRdnInfo,
} from "./lib/utils.jsx";
import { getApiErrorMessage } from "../tools.jsx";

const _ = cockpit.gettext;

function formatPolicyAttributes(attrs) {
    const local = [];
    const inherited = [];
    Object.keys(attrs || {}).sort().forEach(name => {
        const values = attrs[name];
        const isInherited = values.includes("inherited");
        const displayValue = (isInherited
            ? values.filter(v => v !== "inherited")
            : values).join(", ");
        const item = { name, value: displayValue, inherited: isInherited };
        if (isInherited) {
            inherited.push(item);
        } else {
            local.push(item);
        }
    });
    return [...local, ...inherited];
}

export class EffectivePwpModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: true,
            error: null,
            report: null,
        };
    }

    componentDidMount() {
        if (this.props.isOpen) {
            this.loadPolicy();
        }
    }

    componentDidUpdate(prevProps) {
        if (!this.props.isOpen) {
            return;
        }
        if (!prevProps.isOpen ||
            prevProps.entryDn !== this.props.entryDn ||
            prevProps.userType !== this.props.userType ||
            prevProps.selector !== this.props.selector) {
            this.loadPolicy();
        }
    }

    getCustomBaseDnForEntry(entryDn, userType) {
        const ndn = entryDn.toLowerCase();
        const rdnInfo = getRdnInfo(ndn);
        return ndn.replace(rdnInfo.rdnAttr + '=' + rdnInfo.rdnVal + ",", '');
    }

    loadPolicy() {
        const {
            serverId,
            suffixList,
            entryDn,
            userType,
            selector,
        } = this.props;

        if (!entryDn || !userType || !selector) {
            return;
        }

        this.setState({
            loading: true,
            error: null,
            report: null,
        });

        const baseDn = getBaseDnForEntry(entryDn, suffixList);
        if (baseDn === "") {
            this.setState({
                loading: false,
                error: getApiErrorMessage(_("Unable to determine base DN for entry: " + entryDn)),
            });
            return;
        }

        const ndn = entryDn.toLowerCase();
        const rdnInfo = getRdnInfo(ndn);
        const parentBaseDn = ndn.replace(rdnInfo.rdnAttr + '=' + rdnInfo.rdnVal + ",", '');
        fetchUserEffectivePasswordPolicy(serverId, baseDn, parentBaseDn, userType, selector)
                .done(content => {
                    this.setState({
                        loading: false,
                        report: JSON.parse(content),
                    });
                })
                .fail(err => {
                    this.setState({
                        loading: false,
                        error: getApiErrorMessage(err),
                    });
                });
    }

    render() {
        const { isOpen, onClose, entryDn } = this.props;
        const { loading, error, report } = this.state;
        const policyAttrs = report ? formatPolicyAttributes(report.attrs) : [];

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Effective Password Policy")}
                isOpen={isOpen}
                onClose={onClose}
                actions={[
                    <Button key="close" variant="primary" onClick={onClose}>
                        {_("Close")}
                    </Button>
                ]}
            >
                {loading &&
                    <div className="ds-center ds-margin-top-xlg ds-margin-bottom-md">
                        <Spinner size="xl" />
                    </div>}
                {!loading && error &&
                    <Alert variant="danger" isInline title={_("Unable to load password policy")}>
                        {error}
                    </Alert>}
                {!loading && !error && report &&
                    <>
                        <DescriptionList
                            isHorizontal
                            termWidth="10rem"
                            className="ds-margin-top"
                        >
                            <DescriptionListGroup>
                                <DescriptionListTerm>{_("User DN")}</DescriptionListTerm>
                                <DescriptionListDescription>{report.dn || entryDn}</DescriptionListDescription>
                            </DescriptionListGroup>
                            <DescriptionListGroup>
                                <DescriptionListTerm>{_("Policy Type")}</DescriptionListTerm>
                                <DescriptionListDescription>{report.policy_type}</DescriptionListDescription>
                            </DescriptionListGroup>
                            <DescriptionListGroup>
                                <DescriptionListTerm>{_("Policy DN")}</DescriptionListTerm>
                                <DescriptionListDescription>{report.policy_dn}</DescriptionListDescription>
                            </DescriptionListGroup>
                            <DescriptionListGroup>
                                <DescriptionListTerm>{_("Policy Target")}</DescriptionListTerm>
                                <DescriptionListDescription>{report.policy_target}</DescriptionListDescription>
                            </DescriptionListGroup>
                        </DescriptionList>
                        <Divider />
                        <DescriptionList
                            isHorizontal
                            columnModifier={{ default: "2Col" }}
                            termWidth="14rem"
                        >
                            {policyAttrs.map(item => (
                                <DescriptionListGroup key={item.name}>
                                    <DescriptionListTerm>{item.name}</DescriptionListTerm>
                                    <DescriptionListDescription>
                                        {item.value}
                                        {item.inherited ? ` (${_("inherited")})` : ""}
                                    </DescriptionListDescription>
                                </DescriptionListGroup>
                            ))}
                        </DescriptionList>
                    </>}
            </Modal>
        );
    }
}

EffectivePwpModal.propTypes = {
    isOpen: PropTypes.bool.isRequired,
    onClose: PropTypes.func.isRequired,
    serverId: PropTypes.string.isRequired,
    suffixList: PropTypes.array.isRequired,
    entryDn: PropTypes.string,
    userType: PropTypes.string,
    selector: PropTypes.string,
};
