import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Alert,
    Button,
    Card,
    CardBody,
    Modal,
    ModalVariant,
    Spinner,
} from "@patternfly/react-core";
import {
    CheckIcon,
    CopyIcon,
} from "@patternfly/react-icons";
import { getBaseLevelEntryFullAttributes } from "./lib/utils.jsx";

const _ = cockpit.gettext;

export class ViewEntryModal extends React.Component {
    constructor(props) {
        super(props);
        this.loadId = 0;
        this.copyTimeoutId = null;
        this.state = {
            loading: true,
            error: null,
            entryLdif: "",
            copied: false,
        };

        this.clearCopyTimeout = () => {
            if (this.copyTimeoutId) {
                clearTimeout(this.copyTimeoutId);
                this.copyTimeoutId = null;
            }
        };

        this.copyToClipboard = () => {
            const text = this.state.entryLdif;
            if (!text) {
                return;
            }
            try {
                navigator.clipboard.writeText(text)
                        .then(() => {
                            this.setState({ copied: true });
                            this.clearCopyTimeout();
                            this.copyTimeoutId = setTimeout(() => {
                                this.setState({ copied: false });
                                this.copyTimeoutId = null;
                            }, 3000);
                        })
                        .catch(e => console.error('Text could not be copied: ', e ? e.toString() : ""));
            } catch (error) {
                console.error('Text could not be copied: ', error.toString());
            }
        };
    }

    componentDidMount() {
        if (this.props.isOpen) {
            this.loadEntry();
        }
    }

    componentWillUnmount() {
        this.clearCopyTimeout();
    }

    componentDidUpdate(prevProps) {
        if (prevProps.isOpen && !this.props.isOpen) {
            this.loadId += 1;
            this.clearCopyTimeout();
            this.setState({
                loading: true,
                error: null,
                entryLdif: "",
                copied: false,
            });
            return;
        }
        if (!this.props.isOpen) {
            return;
        }
        if (!prevProps.isOpen || prevProps.entryDn !== this.props.entryDn) {
            this.loadEntry();
        }
    }

    loadEntry() {
        const { serverId, entryDn } = this.props;
        if (!entryDn) {
            return;
        }

        const loadId = ++this.loadId;
        this.clearCopyTimeout();
        this.setState({
            loading: true,
            error: null,
            entryLdif: "",
            copied: false,
        });

        getBaseLevelEntryFullAttributes(serverId, entryDn, (result) => {
            if (loadId !== this.loadId) {
                return;
            }
            if (result && result.trim() !== "") {
                this.setState({
                    loading: false,
                    entryLdif: result,
                });
            } else {
                this.setState({
                    loading: false,
                    error: cockpit.format(_("Unable to load LDAP entry: $0"), entryDn),
                });
            }
        });
    }

    render() {
        const { isOpen, onClose, entryDn } = this.props;
        const { loading, error, entryLdif, copied } = this.state;

        let nb = -1;
        const ldifLines = entryLdif.split('\n').map(line => {
            nb++;
            return { data: line, id: nb };
        });

        return (
            <Modal
                variant={ModalVariant.large}
                title={_("View LDAP Entry")}
                isOpen={isOpen}
                onClose={onClose}
                actions={[
                    <Button
                        key="copy"
                        variant="secondary"
                        onClick={this.copyToClipboard}
                        isDisabled={loading || !!error || !entryLdif}
                        icon={copied ? <CheckIcon /> : <CopyIcon />}
                    >
                        {copied ? _("Copied") : _("Copy to clipboard")}
                    </Button>,
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
                    <Alert variant="danger" isInline title={_("Unable to load LDAP entry")}>
                        {error}
                    </Alert>}
                {!loading && !error &&
                    <div>
                        {entryDn &&
                            <Alert
                                variant="info"
                                isInline
                                title={entryDn}
                            />}
                        <Card isSelectable>
                            <CardBody className="ds-textarea">
                                {ldifLines.map((line) => (
                                    <h6 key={line.id}>{line.data}</h6>
                                ))}
                            </CardBody>
                        </Card>
                    </div>}
            </Modal>
        );
    }
}

ViewEntryModal.propTypes = {
    isOpen: PropTypes.bool.isRequired,
    onClose: PropTypes.func.isRequired,
    serverId: PropTypes.string.isRequired,
    entryDn: PropTypes.string,
};
