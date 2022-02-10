import "./css/patternfly-4-cockpit.scss";
// import "core-js/stable";
import React from "react";
import ReactDOM from "react-dom";
import "./css/ds.css";
import "./css/branding.css";
import { DSInstance } from "./ds.jsx";

document.addEventListener("DOMContentLoaded", function () {
    ReactDOM.render(React.createElement(DSInstance, {}), document.getElementById('dsinstance'));
});
