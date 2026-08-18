import cockpit from "cockpit";
import React from "react";
import {
    FormHelperText,
    GridItem,
    ValidatedOptions,
} from "@patternfly/react-core";

const _ = cockpit.gettext;

/*
 * Password-policy number ranges from slapd validation
 * (ldap/servers/slapd/libglobs.c, modify.c, slap.h).
 * Duration/age fields use MAX_ALLOWED_TIME_IN_SECS (INT32_MAX).
 */
export const INT32_MAX = 2147483647;
const TPR_DELAY_MAX = 7 * 24 * 3600; /* 1 week in seconds */

export const FIELD_RANGES = {
    passwordinhistory: { min: 0, max: 24 },
    passwordminlength: { min: 2, max: 512 },
    passwordmindigits: { min: 0, max: 64 },
    passwordminalphas: { min: 0, max: 64 },
    passwordminuppers: { min: 0, max: 64 },
    passwordminlowers: { min: 0, max: 64 },
    passwordminspecials: { min: 0, max: 64 },
    passwordmin8bit: { min: 0, max: 64 },
    passwordmaxrepeats: { min: 0, max: 64 },
    passwordmincategories: { min: 1, max: 5 },
    passwordmintokenlength: { min: 1, max: 64 },
    passwordmaxfailure: { min: 1, max: 32767 },
    passwordmaxsequence: { min: 0, max: 10 },
    passwordmaxseqsets: { min: 0, max: 10 },
    passwordmaxclasschars: { min: 0, max: 1024 },
    passwordresetfailurecount: { min: 0, max: INT32_MAX },
    passwordlockoutduration: { min: 0, max: INT32_MAX },
    passwordminage: { min: 0, max: INT32_MAX },
    passwordmaxage: { min: 0, max: INT32_MAX },
    passwordgracelimit: { min: 0, max: INT32_MAX },
    passwordwarning: { min: 0, max: INT32_MAX },
    passwordtprmaxuse: { min: -1, max: 255 },
    passwordtprdelayexpireat: { min: -1, max: TPR_DELAY_MAX },
    passwordtprdelayvalidfrom: { min: -1, max: TPR_DELAY_MAX },
};

// Strips create_ prefix to find the base field name in FIELD_RANGES
const baseFieldName = (attr) => {
    return attr.startsWith("create_") ? attr.substring(7) : attr;
};

// Returns true if the value is invalid for the given field
export const validateField = (attr, value) => {
    const range = FIELD_RANGES[baseFieldName(attr)];
    if (!range) return false;
    const num = Number(value);
    if (value === "" || isNaN(num) || !Number.isInteger(num) || num < range.min) {
        return true;
    }
    if (range.max !== undefined && num > range.max) {
        return true;
    }
    return false;
};

// Returns a new invalidFields map after validating a field change
export const updateFieldValidation = (invalidFields, attr, value) => {
    const newInvalidFields = { ...invalidFields };
    if (FIELD_RANGES[baseFieldName(attr)]) {
        if (validateField(attr, value)) {
            newInvalidFields[attr] = true;
        } else {
            delete newInvalidFields[attr];
        }
    }
    return newInvalidFields;
};

// Returns { min, max?, validated } props for legacy TextInput number fields.
// Prefer DsNumberInput with fieldName and invalidFields for new code.
export const getValidationProps = (fieldName, invalidFields) => {
    const range = FIELD_RANGES[baseFieldName(fieldName)];
    if (!range) return {};
    const props = { min: range.min };
    if (range.max !== undefined) {
        props.max = range.max;
    }
    props.validated = invalidFields[fieldName]
        ? ValidatedOptions.error
        : ValidatedOptions.default;
    return props;
};

// Returns FormHelperText JSX when the field is invalid, or null
export const renderValidationError = (fieldName, invalidFields) => {
    const range = FIELD_RANGES[baseFieldName(fieldName)];
    if (!range || !invalidFields[fieldName]) return null;
    const msg =
        range.max !== undefined
            ? cockpit.format(
                _("Value must be a number from $0 to $1"),
                range.min,
                range.max
            )
            : cockpit.format(
                _("Value must be an integer greater than or equal to $0"),
                range.min
            );
    return (
        <GridItem span={5}>
            <FormHelperText className="ds-left-margin">
                {msg}
            </FormHelperText>
        </GridItem>
    );
};

// Returns true if any attr in the group is invalid
export const hasInvalidField = (attrs, invalidFields) => {
    return attrs.some((a) => invalidFields[a]);
};
