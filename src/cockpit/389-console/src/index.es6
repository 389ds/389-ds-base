import React from "react";
import ReactDOM from "react-dom";
import "./patternfly/patternfly-cockpit.scss";
import "./css/ds.css";
import "./css/branding.css";
import { DSInstance } from "./ds.jsx";

document.addEventListener("DOMContentLoaded", function () {
    ReactDOM.render(React.createElement(DSInstance, {}), document.getElementById('dsinstance'));
});
