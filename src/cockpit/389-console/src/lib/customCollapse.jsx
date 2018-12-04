import React from "react";
import PropTypes from "prop-types";
import classNames from "classnames";
import { Icon, Button } from "patternfly-react";

class CustomCollapse extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            open: false
        };
    }

    render() {
        const { children, className, textOpened, textClosed } = this.props;
        const { open } = this.state;

        return (
            <div>
                <Button
                    className={classNames("ds-accordion", className)}
                    onClick={() => {
                        this.setState({ open: !open });
                    }}
                >
                    <Icon
                        type="fa"
                        size="1,5x"
                        name={open ? "caret-down" : "caret-right"}
                    />{" "}
                    {open ? textOpened : textClosed}
                </Button>
                <div className="ds-accordion-panel">{open && children}</div>
            </div>
        );
    }
}

CustomCollapse.propTypes = {
    children: PropTypes.any.isRequired,
    /** Top-level custom class */
    className: PropTypes.string,
    /** Text for the link in opened state */
    textOpened: PropTypes.string,
    /** Text for the link in closed state */
    textClosed: PropTypes.string
};

CustomCollapse.defaultProps = {
    className: "",
    textClosed: "Show Advanced Settings",
    textOpened: "Hide Advanced Settings"
};

export default CustomCollapse;
