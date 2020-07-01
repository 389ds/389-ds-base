import React from "react";
import PropTypes from "prop-types";
import {
    noop,
    Row,
    Col,
    ControlLabel,
    Form,
} from "patternfly-react";

export class SuffixConfig extends React.Component {
    render() {
        let cacheInputs;
        if (this.props.autoTuning) {
            const cacheValue = this.props.cachesize + "  (auto-sized)";
            const cachememValue = this.props.cachememsize + "  (auto-sized)";
            cacheInputs =
                <Form horizontal>
                    <Row className="ds-margin-top" title="The entry cache size in bytes setting is being auto-sized and is read-only - see Global Database Configuration">
                        <Col componentClass={ControlLabel} sm={4}>
                            Entry Cache Size
                        </Col>
                        <Col sm={8}>
                            <input disabled value={cachememValue} className="ds-input-auto" type="text" id="cachememsize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The entry cache max entries setting is being auto-sized and is read-only - see Global Database Configuration">
                        <Col componentClass={ControlLabel} sm={4}>
                            Entry Cache Max Entries
                        </Col>
                        <Col sm={8}>
                            <input disabled value={cacheValue} className="ds-input-auto" type="text" id="cachesize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The available memory space in bytes for the DN cache. The DN cache is similar to the entry cache for a database, only its table stores only the entry ID and the entry DN (nsslapd-dncachememsize).">
                        <Col componentClass={ControlLabel} sm={4}>
                            DN Cache Size
                        </Col>
                        <Col sm={8}>
                            <input onChange={this.props.handleChange} value={this.props.dncachememsize} className="ds-input-auto" type="text" id="dncachememsize" />
                        </Col>
                    </Row>
                </Form>;
        } else {
            cacheInputs =
                <Form horizontal>
                    <Row className="ds-margin-top" title="The size for the available memory space in bytes for the entry cache (nsslapd-cachememsize).">
                        <Col componentClass={ControlLabel} sm={4}>
                            Entry Cache Size
                        </Col>
                        <Col sm={8}>
                            <input onChange={this.props.handleChange} value={this.props.cachememsize} className="ds-input-auto" type="text" id="cachememsize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The number of entries to keep in the entry cache, use'-1' for unlimited (nsslapd-cachesize).">
                        <Col componentClass={ControlLabel} sm={4}>
                            Entry Cache Max Entries
                        </Col>
                        <Col sm={8}>
                            <input onChange={this.props.handleChange} value={this.props.cachesize} className="ds-input-auto" type="text" id="cachesize" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="the available memory space in bytes for the DN cache. The DN cache is similar to the entry cache for a database, only its table stores only the entry ID and the entry DN (nsslapd-dncachememsize).">
                        <Col componentClass={ControlLabel} sm={4}>
                            DN Cache Size
                        </Col>
                        <Col sm={8}>
                            <input onChange={this.props.handleChange} value={this.props.dncachememsize} className="ds-input-auto" type="text" id="dncachememsize" />
                        </Col>
                    </Row>
                </Form>;
        }
        return (
            <div className="ds-margin-top-lg">
                {cacheInputs}
                <Form horizontal>
                    <Row className="ds-margin-top-lg" title="Put database in Read-Only mode (nsslapd-readonly).">
                        <Col componentClass={ControlLabel} sm={4}>
                            Database Read-Only Mode
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.props.handleChange} checked={this.props.readOnly} className="ds-config-checkbox" id="readOnly" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Block unindexed searches on this suffix (nsslapd-require-index).">
                        <Col componentClass={ControlLabel} sm={4}>
                            Block Unindexed Searches
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.props.handleChange} checked={this.props.requireIndex} className="ds-config-checkbox" id="requireIndex" />
                        </Col>
                    </Row>
                </Form>
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
