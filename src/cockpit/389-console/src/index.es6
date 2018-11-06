import React from "react";
import ReactDOM from "react-dom";
import { Plugins } from "./plugins.jsx";

document.addEventListener("DOMContentLoaded", function() {
    ReactDOM.render(
        React.createElement(Plugins, {}),
        document.getElementById("plugins")
    );
});
