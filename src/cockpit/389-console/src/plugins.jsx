import cockpit from "cockpit";
import React from "react";
import "./plugins.scss";

const _ = cockpit.gettext;

export class Plugins extends React.Component {
    constructor() {
        super();
        this.state = {
            hostname: _("Unknown")
        };

        cockpit
                .file("/etc/hostname")
                .read()
                .done(content => {
                    this.setState({ hostname: content.trim() });
                });
    }

    render() {
        return (
            <div className="container-fluid">
                <h2>Plugins</h2>
                <p>
                    {cockpit.format(
                        _("Setting up plugins on server $0"),
                        this.state.hostname
                    )}
                </p>
            </div>
        );
    }
}
