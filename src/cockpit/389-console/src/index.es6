import React from "react";
import ReactDOM from "react-dom";
import { Plugins } from "./plugins.jsx";

var serverIdElem;

function renderReactDOM() {
    serverIdElem = document.getElementById("select-server");
    const element = (
        <Plugins serverId={serverIdElem.value.replace("slapd-", "")} />
    );
    ReactDOM.render(element, document.getElementById("plugins"));
}

document.addEventListener("DOMContentLoaded", function() {
    var init_config = setInterval(function() {
        serverIdElem = document.getElementById("select-server");
        if (serverIdElem != null && serverIdElem.value.startsWith("slapd-")) {
            document
                    .getElementById("select-server")
                    .addEventListener("change", renderReactDOM);
            document
                    .getElementById("start-server-btn")
                    .addEventListener("click", renderReactDOM);
            document
                    .getElementById("restart-server-btn")
                    .addEventListener("click", renderReactDOM);
            renderReactDOM();
            clearInterval(init_config);
        }
    }, 250);
});
