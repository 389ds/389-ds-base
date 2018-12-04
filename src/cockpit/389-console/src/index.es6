import React from "react";
import ReactDOM from "react-dom";
import { Plugins } from "./plugins.jsx";

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
    ReactDOM.render(
        <Plugins serverId={serverIdElem} />,
        document.getElementById("plugins")
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
                    .getElementById("start-server-btn")
                    .addEventListener("click", renderReactWrapper);
            document
                    .getElementById("restart-server-btn")
                    .addEventListener("click", renderReactWrapper);
            document
                    .getElementById("remove-server-btn")
                    .addEventListener("click", renderClearWrapper);
            renderReactDOM();
            clearInterval(init_config);
        }
    }, 250);
});
