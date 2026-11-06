import React from "react";
import { Form as PfForm } from "@patternfly/react-core";

/**
 * Prevent implicit HTML form submit from reloading the Cockpit frame.
 * Stops React propagation so portaled modals (same React tree, different DOM)
 * do not trigger parent <Form> submit handlers.
 */
export function onFormSubmitPreventReload(onSubmit) {
    return (event) => {
        event.preventDefault();
        if (typeof event.stopPropagation === "function") {
            event.stopPropagation();
        }
        if (onSubmit) {
            onSubmit(event);
        }
    };
}

/**
 * PatternFly modal body form: Enter runs actionHandler when not disabled.
 */
export function modalFormEnterSubmit(formId, actionHandler, submitDisabled) {
    return {
        id: formId,
        onSubmit: onFormSubmitPreventReload(() => {
            if (!submitDisabled) {
                actionHandler();
            }
        }),
    };
}

/**
 * Primary modal footer button associated with modalFormEnterSubmit via form id.
 */
export function modalPrimaryEnterSubmit(formId, submitDisabled) {
    return {
        type: "submit",
        form: formId,
        isDisabled: submitDisabled,
    };
}

/** Drop-in PatternFly Form replacement; always blocks implicit document reload. */
export function DsForm({ onSubmit, ...props }) {
    return <PfForm {...props} onSubmit={onFormSubmitPreventReload(onSubmit)} />;
}
