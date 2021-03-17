import React from "react";
import PropTypes from "prop-types";
import {
    Checkbox,
    Col,
    Form,
    Icon,
    MessageDialog,
    Row,
} from "patternfly-react";
import {
    Button,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Spinner,
    // TextInput,
    noop
} from "@patternfly/react-core";

class ConfirmPopup extends React.Component {
    constructor(props) {
        super(props);
        this.state = {};

        // Chaining OIDs
        this.primaryAction = this.primaryAction.bind(this);
    }

    primaryAction() {
        this.props.actionFunc(this.props.actionParam);
        this.props.closeHandler();
    }

    render() {
        const {
            showModal,
            closeHandler,
        } = this.props;

        let secondaryContent = "";
        if (this.props.msgContent !== undefined) {
            if (this.props.msgContent.constructor === Array) {
                // Comma separate the lines of this list
                secondaryContent = this.props.msgContent.map((item) =>
                    <p key={item}><b>{item}</b></p>);
            } else {
                secondaryContent = <p><b>{this.props.msgContent}</b></p>;
            }
        }

        const icon = <Icon type="pf" style={{'fontSize':'30px', 'marginRight': '15px'}}
            name="warning-triangle-o" />;
        const msg = <p className="lead">{this.props.msg}</p>;

        return (
            <React.Fragment>
                <MessageDialog
                    className="ds-confirm"
                    show={showModal}
                    onHide={closeHandler}
                    primaryAction={this.primaryAction}
                    secondaryAction={closeHandler}
                    primaryActionButtonContent="Yes"
                    secondaryActionButtonContent="No"
                    title="Confirmation"
                    icon={icon}
                    primaryContent={msg}
                    secondaryContent={secondaryContent}
                    accessibleName="questionDialog"
                    accessibleDescription="questionDialogContent"
                />
            </React.Fragment>
        );
    }
}

export class DoubleConfirmModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            actionHandler,
            checked,
            spinning,
            item,
            mTitle,
            mMsg,
            mSpinningMsg,
            mBtnName,
        } = this.props;
        let spinner = "";
        let saveDisabled = true;

        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />{mSpinningMsg}
                    </div>
                </Row>;
            saveDisabled = true;
        }
        if (checked) {
            saveDisabled = false;
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title={mTitle}
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={actionHandler} isDisabled={saveDisabled}>
                        {mBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h4>{mMsg}</h4>
                    <h5 className="ds-center ds-margin-top-xlg"><b>{item}</b></h5>
                    <Row className="ds-margin-top-xlg">
                        <Col sm={12} className="ds-center">
                            <Checkbox
                                id="modalChecked"
                                defaultChecked={checked}
                                onChange={handleChange}
                            >
                                <b>Yes</b>, I am sure.
                            </Checkbox>
                        </Col>
                    </Row>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

DoubleConfirmModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    actionHandler: PropTypes.func,
    spinning: PropTypes.bool,
    item: PropTypes.string,
    checked: PropTypes.bool,
    mTitle: PropTypes.string,
    mMsg: PropTypes.string,
    mSpinningMsg: PropTypes.string,
    mBtnName: PropTypes.string,
};

DoubleConfirmModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    actionHandler: noop,
    spinning: false,
    item: "",
    checked: false,
    mTitle: "",
    mMsg: "",
    mSpinningMsg: "",
    mBtnName: "",
};

export { ConfirmPopup };
