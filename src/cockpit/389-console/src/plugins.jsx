import cockpit from "cockpit";
import React from "react";
import "./plugins.scss";
import { Checkbox } from "@patternfly/react-core";

const _ = cockpit.gettext;

export class Plugins extends React.Component {
    constructor() {
        super();
        this.state = {
            hostname: _("Unknown"),
            checked: true
        };
        this.handleChange = this.handleChange.bind(this);

        cockpit
                .file("/etc/hostname")
                .read()
                .done(content => {
                    this.setState({ hostname: content.trim() });
                });
    }

    handleChange(event) {
        this.setState({ checked: event.checked });
    }

    render() {
        function go_up() {
            cockpit.jump("/389-console", cockpit.transport.host);
        }
        return (
            <div className="container-fluid">
                <h2>Plugins</h2>
                <p>
                    <a onClick={go_up}>{_("Back to main screen")}</a>
                </p>
                <p>
                    {cockpit.format(
                        _("Setting up plugins on server $0"),
                        this.state.hostname
                    )}
                </p>
                <Checkbox
                    label="React component checkbox"
                    checked={this.state.checked}
                    onChange={this.handleChange}
                />
            </div>
        );
    }
}
