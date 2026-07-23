import React from "react";
import PropTypes from "prop-types";
import { NumberInput, ValidatedOptions } from "@patternfly/react-core";
import { FIELD_RANGES, INT32_MAX as PWP_INT32_MAX } from "./database/pwpValidation.jsx";

export const INT32_MAX = PWP_INT32_MAX;

/*
 * Resolve min/max for a DsNumberInput. Prefer explicit min/max props; otherwise
 * look up FIELD_RANGES via fieldName. Every DsNumberInput must end up with both.
 */
export function getNumberFieldRange(fieldName, minProp, maxProp) {
    if (minProp !== undefined && maxProp !== undefined) {
        return { min: minProp, max: maxProp };
    }
    const base = fieldName && fieldName.startsWith("create_")
        ? fieldName.substring(7)
        : fieldName;
    const range = base && FIELD_RANGES[base];
    if (range && range.min !== undefined && range.max !== undefined) {
        return {
            min: minProp !== undefined ? minProp : range.min,
            max: maxProp !== undefined ? maxProp : range.max,
        };
    }
    if (minProp !== undefined || maxProp !== undefined) {
        return {
            min: minProp !== undefined ? minProp : 0,
            max: maxProp !== undefined ? maxProp : INT32_MAX,
        };
    }

    return { min: 0, max: INT32_MAX };
}

function isAllowedInput(raw, min, max) {
    if (raw === "") {
        return false;
    }
    if (raw === "-" && min < 0) {
        return true;
    }
    if (max !== undefined && Number(raw) > max) {
        return false;
    }
    return /^-?\d+$/.test(raw);
}

export function clampInteger(value, min, max) {
    let num = Number(value);
    if (isNaN(num)) {
        num = min;
    }
    num = Math.trunc(num);
    return Math.min(max, Math.max(min, num));
}

export function createValidatedChangeEvent(id, value) {
    return { target: { id, value: String(value) } };
}

// PatternFly NumberInput only treats typeof value === 'number' for +/- button
// enablement; string values are treated as 0, which disables minus at min > 0.
export function toNumberInputValue(value) {
    if (value === "" || value === "-") {
        return "";
    }
    const num = Number(value);
    return isNaN(num) ? "" : num;
}

export function DsNumberInput({
    id,
    value,
    fieldName,
    min,
    max,
    invalidFields,
    validated,
    onChange,
    isDisabled,
    title,
    widthChars = 6,
    className,
    "aria-describedby": ariaDescribedBy,
}) {
    const range = getNumberFieldRange(fieldName, min, max);

    const handleChange = (event) => {
        const raw = event.target.value;
        if (!isAllowedInput(raw, range.min, range.max)) {
            return;
        }
        if (onChange) {
            onChange(createValidatedChangeEvent(id, raw));
        }
    };

    const handleMinus = () => {
        if (isDisabled || !onChange) {
            return;
        }
        const current = value === "" || value === "-" ? range.min : Number(value);
        const newValue = clampInteger(current - 1, range.min, range.max);
        onChange(createValidatedChangeEvent(id, newValue));
    };

    const handlePlus = () => {
        if (isDisabled || !onChange) {
            return;
        }
        const current = value === "" || value === "-" ? range.min : Number(value);
        const newValue = clampInteger(current + 1, range.min, range.max);
        onChange(createValidatedChangeEvent(id, newValue));
    };

    let validatedState = validated;
    if (validatedState === undefined && fieldName && invalidFields) {
        validatedState = invalidFields[fieldName]
            ? ValidatedOptions.error
            : ValidatedOptions.default;
    }

    const numberValue = toNumberInputValue(value);

    return (
        <NumberInput
            id={id}
            title={title}
            value={numberValue}
            min={range.min}
            max={range.max}
            isDisabled={isDisabled}
            className={className}
            validated={validatedState}
            onMinus={handleMinus}
            onChange={handleChange}
            onPlus={handlePlus}
            inputName={id || "input"}
            inputAriaLabel={id || "number input"}
            minusBtnAriaLabel="minus"
            plusBtnAriaLabel="plus"
            widthChars={widthChars}
        />
    );
}

DsNumberInput.propTypes = {
    id: PropTypes.string,
    value: PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
    fieldName: PropTypes.string,
    min: PropTypes.number,
    max: PropTypes.number,
    invalidFields: PropTypes.object,
    validated: PropTypes.oneOf(Object.values(ValidatedOptions)),
    onChange: PropTypes.func,
    isDisabled: PropTypes.bool,
    title: PropTypes.string,
    widthChars: PropTypes.number,
    className: PropTypes.string,
    "aria-describedby": PropTypes.string,
};
