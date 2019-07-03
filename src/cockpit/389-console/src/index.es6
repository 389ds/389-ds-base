import React from "react";
import ReactDOM from "react-dom";
import { Plugins } from "./plugins.jsx";
import { Database } from "./database.jsx";
import { Monitor } from "./monitor.jsx";
import { Security } from "./security.jsx";

var serverIdElem;

function renderReactDOM(clear) {
    // We should clear the React properties on the instance removal
    if (clear) {
        serverIdElem = "";
    } else {
        serverIdElem = document
                .getElementById("select-server")
                .value.replace("slapd-", "");
    }
    let d = new Date();
    let tabKey = d.getTime();

    // Plugins Tab
    ReactDOM.render(
        <Plugins serverId={serverIdElem} key={tabKey} />,
        document.getElementById("plugins")
    );

    // Database tab
    ReactDOM.render(
        <Database serverId={serverIdElem} key={tabKey} />,
        document.getElementById("database")
    );

    // Monitor tab
    ReactDOM.render(
        <Monitor serverId={serverIdElem} key={tabKey} />,
        document.getElementById("monitor")
    );

    // Security tab
    ReactDOM.render(
        <Security serverId={serverIdElem} key={tabKey} />,
        document.getElementById("security")
    );
}

// We have to create the wrappers because this is no simple way
// to pass arguments to the listener's callback function
function renderReactWrapper() {
    renderReactDOM(false);
}
function renderClearWrapper() {
    renderReactDOM(true);
}

document.addEventListener("DOMContentLoaded", function() {
    var init_config = setInterval(function() {
        serverIdElem = document.getElementById("select-server");
        if (serverIdElem != null && serverIdElem.value.startsWith("slapd-")) {
            document
                    .getElementById("select-server")
                    .addEventListener("change", renderReactWrapper);
            document
                    .getElementById("reload-page")
                    .addEventListener("click", renderReactWrapper);
            document
                    .getElementById("remove-server-btn")
                    .addEventListener("click", renderClearWrapper);
            renderReactDOM();
            clearInterval(init_config);
        }
    }, 250);
});
