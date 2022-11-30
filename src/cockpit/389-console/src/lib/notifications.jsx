import React from "react";
import PropTypes from "prop-types";
import {
    Button,
    Checkbox,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";

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
            extraPrimaryProps.spinnerAriaValueText = "Loading";
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title={mTitle}
                titleIconVariant="warning"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Loading" : undefined}
                        variant="primary"
                        onClick={actionHandler}
                        isDisabled={saveDisabled || spinning}
                        {...extraPrimaryProps}
                    >
                        {btnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
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
                    <Grid className="ds-margin-top-xlg">
                        <GridItem sm={12} className="ds-center">
                            <Checkbox
                                id="modalChecked"
                                isChecked={checked}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label={<><b>Yes</b>, I am sure.</>}
                            />
                        </GridItem>
                    </Grid>
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
