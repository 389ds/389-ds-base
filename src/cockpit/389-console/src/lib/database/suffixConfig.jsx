import React from "react";
import "../../css/ds.css";
import PropTypes from "prop-types";
import {
    noop,
    Row,
    Col,
    ControlLabel,
} from "patternfly-react";

export class SuffixConfig extends React.Component {
    render() {
        let cacheInputs;
        if (this.props.autoTuning) {
            const cacheValue = this.props.cachesize + "  (auto-sized)";
            const cachememValue = this.props.cachememsize + "  (auto-sized)";
            cacheInputs =
                <div>
                    <Row className="ds-margin-top" title="The entry cache size setting is being auto-sized and is read-only - see Global Database Configuration">
                        <Col componentClass={ControlLabel} sm={3}>
                            Entry Cache Size (bytes)
                        </Col>
                        <Col sm={7}>
                            <input disabled value={cachememValue} className="ds-input-auto" type="text" id="cachememsize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The entry cache max entries setting is being auto-sized and is read-only - see Global Database Configuration">
                        <Col componentClass={ControlLabel} sm={3}>
                            Entry Cache Max Entries
                        </Col>
                        <Col sm={7}>
                            <input disabled value={cacheValue} className="ds-input-auto" type="text" id="cachesize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The available memory space for the DN cache. The DN cache is similar to the entry cache for a database, only its table stores only the entry ID and the entry DN (nsslapd-dncachememsize).">
                        <Col componentClass={ControlLabel} sm={3}>
                            DN Cache Size (bytes)
                        </Col>
                        <Col sm={7}>
                            <input onChange={this.props.handleChange} value={this.props.dncachememsize} className="ds-input-auto" type="text" id="dncachememsize" />
                        </Col>
                    </Row>
                </div>;
        } else {
            cacheInputs =
                <div>
                    <Row className="ds-margin-top" title="The size for the available memory space for the entry cache (nsslapd-cachememsize).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Entry Cache Size (bytes)
                        </Col>
                        <Col sm={7}>
                            <input onChange={this.props.handleChange} value={this.props.cachememsize} className="ds-input-auto" type="text" id="cachememsize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The number of entries to keep in the entry cache, use'-1' for unlimited (nsslapd-cachesize).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Entry Cache Max Entries
                        </Col>
                        <Col sm={7}>
                            <input onChange={this.props.handleChange} value={this.props.cachesize} className="ds-input-auto" type="text" id="cachesize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="the available memory space for the DN cache. The DN cache is similar to the entry cache for a database, only its table stores only the entry ID and the entry DN (nsslapd-dncachememsize).">
                        <Col componentClass={ControlLabel} sm={3}>
                            DN Cache Size (bytes)
                        </Col>
                        <Col sm={7}>
                            <input onChange={this.props.handleChange} value={this.props.dncachememsize} className="ds-input-auto" type="text" id="dncachememsize" />
                        </Col>
                    </Row>
                </div>;
        }
        return (
            <div className="ds-margin-top-lg">
                <p />
                {cacheInputs}
                <p />
                <div>
                    <div>
                        <input type="checkbox" onChange={this.props.handleChange} checked={this.props.readOnly} className="ds-config-checkbox" id="readOnly" /><label
                            htmlFor="readOnly" className="ds-label" title="Put database in Read-Only mode (nsslapd-readonly)."> Database Read-Only Mode</label>
                    </div>
                    <div>
                        <input type="checkbox" onChange={this.props.handleChange} checked={this.props.requireIndex} className="ds-config-checkbox" id="requireIndex" /><label
                            htmlFor="requireIndex" className="ds-label" title="Block unindexed searches on this suffix (nsslapd-require-index)."> Block Unindexed Searches</label>
                    </div>
                </div>
                <div className="ds-margin-top-lg">
                    <button className="btn btn-primary save-button" onClick={this.props.saveHandler}>Save Configuration</button>
                </div>
            </div>
        );
    }
}

// Property types and defaults

SuffixConfig.propTypes = {
    cachememsize: PropTypes.string,
    cachesize: PropTypes.string,
    dncachememsize: PropTypes.string,
    readOnly: PropTypes.bool,
    requireIndex: PropTypes.bool,
    autoTuning: PropTypes.bool,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
};

SuffixConfig.defaultProps = {
    cachememsize: "",
    cachesize: "",
    dncachememsize: "",
    readOnly: false,
    requireIndex: false,
    autoTuning: false,
    handleChange: noop,
    saveHandler: noop,
};
