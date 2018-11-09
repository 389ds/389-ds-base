import React from "react";
import PropTypes from "prop-types";
import {
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
                            {notification.message}
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

export default NotificationController;
