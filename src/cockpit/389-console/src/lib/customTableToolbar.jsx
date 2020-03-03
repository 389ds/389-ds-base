import React from "react";
import PropTypes from "prop-types";
import classNames from "classnames";
import {
    noop,
    Spinner,
    Form,
    ControlLabel,
    FormControl
} from "patternfly-react";

class CustomTableToolbar extends React.Component {
    render() {
        const {
            children,
            className,
            placeholder,
            modelToSearch,
            searchFilterValue,
            handleValueChange,
            disableLoadingSpinner,
            loading
        } = this.props;

        const classes = classNames(
            "form-group toolbar-pf-find ds-toolbar",
            className
        );

        return (
            <div className={classes}>
                <Form inline>
                    <ControlLabel className="ds-float-left ds-right-indent">
                        Filter {modelToSearch}
                    </ControlLabel>
                    <div className="ds-float-left">
                        <FormControl
                            type="text"
                            placeholder={placeholder}
                            value={searchFilterValue}
                            onChange={handleValueChange}
                        />
                    </div>
                    <div className="toolbar-pf-action-right">{children}</div>
                    <div className="toolbar-pf-action-right ds-right-indent">
                        {!disableLoadingSpinner && (
                            <Spinner loading={loading} size="md" />
                        )}
                    </div>
                </Form>
            </div>
        );
    }
}

CustomTableToolbar.propTypes = {
    children: PropTypes.any,
    className: PropTypes.string,
    placeholder: PropTypes.string,
    modelToSearch: PropTypes.string,
    searchFilterValue: PropTypes.string,
    handleValueChange: PropTypes.func,
    loading: PropTypes.bool,
    disableLoadingSpinner: PropTypes.bool
};

CustomTableToolbar.defaultProps = {
    className: "",
    placeholder: "",
    modelToSearch: "",
    searchFilterValue: "",
    handleValueChange: noop,
    loading: false,
    disableLoadingSpinner: false
};

export default CustomTableToolbar;
