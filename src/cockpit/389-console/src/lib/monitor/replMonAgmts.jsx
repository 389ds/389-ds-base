import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    AgmtTable,
} from "./monitorTables.jsx";

import {
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSyncAlt } from '@fortawesome/free-solid-svg-icons';

export class ReplAgmtMonitor extends React.Component {
    constructor (props) {
        super(props);

        this.state = {};

        this.pokeAgmt = this.pokeAgmt.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    pokeAgmt (evt) {
        const agmt_name = evt.target.id;
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-agmt", "poke", "--suffix=" + this.props.suffix, agmt_name];
        log_cmd("pokeAgmt", "Awaken the agreement", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Replication agreement has been poked`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to poke replication agreement ${agmt_name} - ${errMsg.desc}`
                    );
                });
    }

    render () {
        const replAgmts = this.props.data.replAgmts;

        return (
            <div>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Monitor Replication Agreements
                            <FontAwesomeIcon
                                size="lg"
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title="Refresh replication monitor"
                                onClick={this.props.reload}
                            />
                        </Text>
                    </TextContent>
                </div>
                <AgmtTable
                    agmts={replAgmts}
                    pokeAgmt={this.pokeAgmt}
                />
            </div>
        );
    }
}

// Props and defaultProps

ReplAgmtMonitor.propTypes = {
    data: PropTypes.object,
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplAgmtMonitor.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
};

export default ReplAgmtMonitor;
