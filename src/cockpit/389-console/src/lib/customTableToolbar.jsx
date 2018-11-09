import React from "react";
import PropTypes from "prop-types";
import classNames from "classnames";
import { noop, Spinner, FormControl } from "patternfly-react";

class CustomTableToolbar extends React.Component {
    render() {
        const {
            className,
            placeholder,
            modelToSearch,
            searchFilterValue,
            handleValueChange,
            loading
        } = this.props;

        const classes = classNames("form-group toolbar-pf-find", className);

        return (
            <div className={classes}>
                <label>
                    <div className="ds-float-left ds-right-indent">
                        Search {modelToSearch}
                    </div>
                    <div className="ds-float-left">
                        <FormControl
                            type="text"
                            id="find"
                            placeholder={placeholder}
                            value={searchFilterValue}
                            onChange={handleValueChange}
                        />
                    </div>
                </label>
                <div className="ds-float-right ds-right-indent">
                    <Spinner loading={loading} size="md" />
                </div>
            </div>
        );
    }
}

CustomTableToolbar.propTypes = {
    className: PropTypes.string,
    placeholder: PropTypes.string,
    modelToSearch: PropTypes.string,
    searchFilterValue: PropTypes.string,
    handleValueChange: PropTypes.func,
    loading: PropTypes.bool
};

CustomTableToolbar.defaultProps = {
    className: "",
    placeholder: "",
    modelToSearch: "",
    searchFilterValue: "",
    handleValueChange: noop,
    loading: false
};

export default CustomTableToolbar;
