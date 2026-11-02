import cockpit from "cockpit";
import { DsForm as Form, modalFormEnterSubmit, modalPrimaryEnterSubmit } from "../dsForm.jsx";
import React from "react";
import PropTypes from "prop-types";
import {
    Button,
    Checkbox,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";

const _ = cockpit.gettext;

const DOUBLE_CONFIRM_MODAL_FORM_ID = "ds-double-confirm-modal-form";
const WARNING_MODAL_FORM_ID = "ds-warning-modal-form";

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
        let saveDisabled = true;
        let btnName = mBtnName;
        const extraPrimaryProps = {};

        if (checked) {
            saveDisabled = false;
        }

        if (spinning) {
            btnName = mSpinningMsg;
            extraPrimaryProps.spinnerAriaValueText = btnName;
        }

        const submitDisabled = saveDisabled || spinning;
        const actions = [];
        if (btnName) {
            actions.push(
                <Button
                    key="confirm"
                    variant="primary"
                    isDisabled={submitDisabled}
                    isLoading={spinning}
                    {...modalPrimaryEnterSubmit(DOUBLE_CONFIRM_MODAL_FORM_ID, submitDisabled)}
                    {...extraPrimaryProps}
                >
                    {btnName}
                </Button>
            );
        }
        actions.push(
            <Button
                key="close"
                variant="link"
                {...(!btnName
                    ? modalPrimaryEnterSubmit(DOUBLE_CONFIRM_MODAL_FORM_ID, false)
                    : { onClick: closeHandler })}
            >
                {_("Close")}
            </Button>
        );
        const formSubmitHandler = btnName ? actionHandler : closeHandler;
        return (
            <Modal
                variant={ModalVariant.small}
                title={mTitle}
                titleIconVariant="warning"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={actions}
            >
                <Form
                    isHorizontal
                    autoComplete="off"
                    {...modalFormEnterSubmit(
                        DOUBLE_CONFIRM_MODAL_FORM_ID,
                        formSubmitHandler,
                        btnName ? submitDisabled : false
                    )}
                >
                    <TextContent>
                        <Text className="ds-margin-top" component={TextVariants.h3}>
                            {mMsg}
                        </Text>
                    </TextContent>
                    <TextContent>
                        <Text className="ds-center ds-margin-top" component={TextVariants.h4}>
                            <i>{item}</i>
                        </Text>
                    </TextContent>
                    {btnName && (
                        <Grid className="ds-margin-top-xlg">
                            <GridItem sm={12} className="ds-center">
                                <Checkbox
                                        id="modalChecked"
                                        isChecked={checked}
                                        isDisabled={spinning}
                                        onChange={(e, checked) => {
                                            handleChange(e);
                                        }}
                                        label={<><b>{_("Yes")}</b>{_(", I am sure.")}</>}
                                    />
                            </GridItem>
                        </Grid>
                    )}
                </Form>
            </Modal>
        );
    }
}

export class WarningModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            mTitle,
            mMsg,
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.small}
                title={mTitle}
                titleIconVariant="warning"
                isOpen={showModal}
                aria-labelledby="warning-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="ok"
                        variant="primary"
                        {...modalPrimaryEnterSubmit(WARNING_MODAL_FORM_ID, false)}
                    >
                        {_("Okay")}
                    </Button>
                ]}
            >
                <Form
                    isHorizontal
                    autoComplete="off"
                    {...modalFormEnterSubmit(WARNING_MODAL_FORM_ID, closeHandler, false)}
                >
                    <TextContent>
                        <Text className="ds-margin-top ds-margin-bottom" component={TextVariants.h3}>
                            {mMsg}
                        </Text>
                    </TextContent>
                </Form>
            </Modal>
        );
    }
}

WarningModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    mTitle: PropTypes.string,
    mMsg: PropTypes.string,
};

WarningModal.defaultProps = {
    showModal: false,
    mTitle: "",
    mMsg: "",
};

DoubleConfirmModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    actionHandler: PropTypes.func,
    spinning: PropTypes.bool,
    checked: PropTypes.bool,
    mTitle: PropTypes.string,
    mMsg: PropTypes.string,
    mSpinningMsg: PropTypes.string,
    mBtnName: PropTypes.string,
};

DoubleConfirmModal.defaultProps = {
    showModal: false,
    spinning: false,
    item: "",
    checked: false,
    mTitle: "",
    mMsg: "",
    mSpinningMsg: "",
    mBtnName: "",
};
