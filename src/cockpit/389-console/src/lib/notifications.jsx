import React from "react";
import PropTypes from "prop-types";
import {
    Icon,
    MessageDialog,
    TimedToastNotification,
    ToastNotificationList
} from "patternfly-react";

class NotificationController extends React.Component {
    render() {
        const { notifications, removeNotificationAction } = this.props;
        return (
            <ToastNotificationList>
                {notifications.map(notification => (
                    <TimedToastNotification
                        key={notification.key}
                        type={notification.type}
                        persistent={notification.persistent}
                        onDismiss={() => removeNotificationAction(notification)}
                        timerdelay={notification.timerdelay} // By default - 8000
                    >
                        <span>
                            {notification.header && (
                                <strong>{notification.header}</strong>
                            )}
                            {notification.type == "error" ? (
                                <pre>{notification.message}</pre>
                            ) : (
                                <span>{notification.message}</span>
                            )}
                        </span>
                    </TimedToastNotification>
                ))}
            </ToastNotificationList>
        );
    }
}

NotificationController.propTypes = {
    removeNotificationAction: PropTypes.func,
    notifications: PropTypes.array
};

NotificationController.defaultProps = {
    notifications: []
};

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

export { NotificationController, ConfirmPopup };
