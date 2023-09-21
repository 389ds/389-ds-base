import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    WinsyncAgmtTable,
} from "./monitorTables.jsx";
import {
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSyncAlt } from '@fortawesome/free-solid-svg-icons';

const _ = cockpit.gettext;

export class ReplAgmtWinsync extends React.Component {
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
            "repl-winsync-agmt", "poke", "--suffix=" + this.props.suffix, agmt_name];
        log_cmd("pokeAgmt", "Awaken the agreement", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Replication Winysnc agreement has been poked")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to poke replication winsync agreement $0 - $1"), agmt_name, errMsg.desc)
                    );
                });
    }

    render () {
        const replWinsyncAgmts = this.props.data.replWinsyncAgmts;

        return (
            <div>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Monitor Winsync Agreements")}
                            <FontAwesomeIcon
                                size="lg"
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title={_("Refresh replication monitor")}
                                onClick={this.props.handleReload}
                            />
                        </Text>
                    </TextContent>
                </div>
                <WinsyncAgmtTable
                    agmts={replWinsyncAgmts}
                    handlePokeAgmt={this.pokeAgmt}
                />
            </div>
        );
    }
}

// Props and defaultProps

ReplAgmtWinsync.propTypes = {
    data: PropTypes.object,
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplAgmtWinsync.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
};

export default ReplAgmtWinsync;
