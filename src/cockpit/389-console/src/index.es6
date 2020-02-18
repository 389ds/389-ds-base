import React from "react";
import ReactDOM from "react-dom";
import { DSInstance } from "./ds.jsx";

document.addEventListener("DOMContentLoaded", function () {
    ReactDOM.render(React.createElement(DSInstance, {}), document.getElementById('dsinstance'));
});
