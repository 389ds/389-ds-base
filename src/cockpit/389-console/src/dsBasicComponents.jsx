import React, { useState, useRef, useEffect, useCallback } from 'react';
import PropTypes from 'prop-types';
import {
    Select,
    SelectOption,
    SelectList,
    MenuToggle,
    TextInputGroup,
    TextInputGroupMain,
    TextInputGroupUtilities,
    Button,
    Label,
    LabelGroup,
} from '@patternfly/react-core';
import { TimesIcon } from '@patternfly/react-icons';

const TypeaheadSelect = ({
    selected,
    onSelect,
    onClear,
    options = [],
    isMulti = false,
    isCreatable = false,
    onCreateOption,
    validateCreate,
    hasCheckbox = false,
    placeholder = "Select an option...",
    noResultsText = "No results found",
    isDisabled = false,
    validated = 'default',
    validationMessage,
    ariaLabel = "Select input",
    className,
    onToggle,
    isOpen: controlledIsOpen,
    allowCustomValues = false,
    openOnClick = true,
    maxOptionsForClickOpen = 20,
}) => {
    const [internalIsOpen, setInternalIsOpen] = useState(false);
    const isOpen = controlledIsOpen !== undefined ? controlledIsOpen : internalIsOpen;
    const [inputValue, setInputValue] = useState('');
    const [selectOptions, setSelectOptions] = useState([]);
    const [focusedItemIndex, setFocusedItemIndex] = useState(null);
    const [activeItemId, setActiveItemId] = useState(null);
    const textInputRef = useRef();
    const isSelectingRef = useRef(false);

    const CREATE_NEW = 'create-new';

    // Normalize options
    const normalizeOptions = (opts) => {
        if (!Array.isArray(opts)) return [];
        return opts.map(opt => {
            if (typeof opt === 'string') {
                return { value: opt, children: opt };
            }
            return { ...opt, children: opt.label || opt.value };
        });
    };

    // Normalize selected values to handle both string and array inputs consistently
    const normalizeSelected = (sel) => {
        if (isMulti) {
            if (Array.isArray(sel)) return sel;
            if (sel === null || sel === undefined || sel === '') return [];
            return [sel];
        } else {
            if (Array.isArray(sel)) return sel.length > 0 ? sel[0] : '';
            return sel || '';
        }
    };

    // Enhanced validation for create operations
    const validateCreateValue = useCallback((value) => {
        if (!value || !value.trim()) return false;

        if (validateCreate) {
            const result = validateCreate(value);
            return typeof result === 'boolean' ? result : result.isValid;
        }

        return true;
    }, [validateCreate]);

    const resetActiveAndFocusedItem = () => {
        setFocusedItemIndex(null);
        setActiveItemId(null);
    };

    const closeMenu = useCallback(() => {
        if (controlledIsOpen === undefined) {
            setInternalIsOpen(false);
        }
        if (onToggle) {
            onToggle(null, false);
        }
        resetActiveAndFocusedItem();
    }, [controlledIsOpen, onToggle]);

    const openMenu = useCallback(() => {
        if (controlledIsOpen === undefined) {
            setInternalIsOpen(true);
        }
        if (onToggle) {
            onToggle(null, true);
        }
    }, [controlledIsOpen, onToggle]);

    // Update select options based on input
    useEffect(() => {
        // Skip recalculation if we're in the middle of a selection to prevent flash
        if (isSelectingRef.current) return;

        const normalizedOptions = normalizeOptions(options);
        const currentSelections = normalizeSelected(selected);
        let newSelectOptions = normalizedOptions;

        // Always ensure selected values are available in options, even if not in original options
        if (isMulti && Array.isArray(currentSelections) && currentSelections.length > 0) {
            const selectedNotInOptions = currentSelections
                .filter(selectedValue => !normalizedOptions.some(opt => opt.value === selectedValue))
                .map(selectedValue => ({ value: selectedValue, children: selectedValue }));

            newSelectOptions = [...normalizedOptions, ...selectedNotInOptions];
        }

        if (inputValue && inputValue.trim()) {
            // Filter options based on input
            newSelectOptions = newSelectOptions.filter(option =>
                String(option.children).toLowerCase().includes(inputValue.toLowerCase())
            );

            // Add create option if applicable
            if (isCreatable) {
                const isValid = validateCreateValue(inputValue);
                const alreadyExists = normalizedOptions.some(opt => opt.value === inputValue);
                const alreadySelected = isMulti ?
                    (Array.isArray(currentSelections) && currentSelections.includes(inputValue)) :
                    selected === inputValue;

                if (isValid && !alreadyExists && !alreadySelected) {
                    newSelectOptions = [...newSelectOptions, {
                        children: `Create "${inputValue}"`,
                        value: CREATE_NEW,
                        isCreatable: true
                    }];
                }
            }
        }
        // When inputValue is empty, newSelectOptions already contains all options (with selected values if multi)

        setSelectOptions(newSelectOptions);
        setFocusedItemIndex(null);
        setActiveItemId(null);
    }, [inputValue, options, isCreatable, validateCreateValue, selected, isMulti]);

    // Separate effect to handle opening menu when there's input value
    useEffect(() => {
        if (inputValue && !isOpen) {
            openMenu();
        }
    }, [inputValue, isOpen, openMenu]);

    const createItemId = (value) => `select-typeahead-${value}`.replace(/\s+/g, '-');

    const setActiveAndFocusedItem = (itemIndex) => {
        setFocusedItemIndex(itemIndex);
        const focusedItem = selectOptions[itemIndex];
        if (focusedItem) {
            setActiveItemId(createItemId(focusedItem.value));
        }
    };

    const onInputClick = () => {
        if (!isOpen) {
            // Allow opening on click based on configuration
            if (openOnClick) {
                const normalizedOptions = normalizeOptions(options);
                // If there's input, always open. If no input, check if options are small enough
                if (inputValue || normalizedOptions.length <= maxOptionsForClickOpen) {
                    openMenu();
                }
            }
        } else if (!inputValue) {
            closeMenu();
        }
    };

    const handleSelect = (value) => {
        if (!value) return;

        // Set flag to prevent options recalculation during selection (prevents flash)
        isSelectingRef.current = true;

        if (value === CREATE_NEW) {
            if (isCreatable && inputValue && validateCreateValue(inputValue)) {
                if (onCreateOption) {
                    onCreateOption(inputValue);
                }

                if (isMulti) {
                    const currentSelections = normalizeSelected(selected);
                    if (!currentSelections.includes(inputValue)) {
                        onSelect(null, [...currentSelections, inputValue]);
                    }
                } else {
                    onSelect(null, inputValue);
                }

                setInputValue('');
                resetActiveAndFocusedItem();
                if (!isMulti) {
                    closeMenu();
                }
            }
        } else {
            if (isMulti) {
                const currentSelections = normalizeSelected(selected);
                const newSelections = currentSelections.includes(value)
                    ? currentSelections.filter(selection => selection !== value)
                    : [...currentSelections, value];
                onSelect(null, newSelections);
                setInputValue('');
            } else {
                // For single select, close menu and clear input
                closeMenu();
                onSelect(null, value);
                setInputValue('');
            }
        }

        resetActiveAndFocusedItem();

        // Reset the flag after a brief delay to allow state updates to complete
        // This ensures proper filter reset while preventing flash during selection
        setTimeout(() => {
            isSelectingRef.current = false;
            // After selection completes, ensure options are reset to the full list
            setSelectOptions(() => {
                const normalizedOptions = normalizeOptions(options);
                const currentSelections = normalizeSelected(selected);
                if (isMulti && Array.isArray(currentSelections) && currentSelections.length > 0) {
                    const selectedNotInOptions = currentSelections
                        .filter(selectedValue => !normalizedOptions.some(opt => opt.value === selectedValue))
                        .map(selectedValue => ({ value: selectedValue, children: selectedValue }));
                    return [...normalizedOptions, ...selectedNotInOptions];
                }
                return normalizedOptions;
            });
        }, 10);

        textInputRef.current?.focus();
    };

    const onTextInputChange = (_event, value) => {
        setInputValue(value);
        resetActiveAndFocusedItem();
    };

    const handleMenuArrowKeys = (key) => {
        let indexToFocus = 0;
        if (!isOpen) {
            openMenu();
        }

        if (selectOptions.every(option => option.isDisabled)) {
            return;
        }

        if (key === 'ArrowUp') {
            if (focusedItemIndex === null || focusedItemIndex === 0) {
                indexToFocus = selectOptions.length - 1;
            } else {
                indexToFocus = focusedItemIndex - 1;
            }
            while (selectOptions[indexToFocus]?.isDisabled) {
                indexToFocus--;
                if (indexToFocus === -1) {
                    indexToFocus = selectOptions.length - 1;
                }
            }
        }

        if (key === 'ArrowDown') {
            if (focusedItemIndex === null || focusedItemIndex === selectOptions.length - 1) {
                indexToFocus = 0;
            } else {
                indexToFocus = focusedItemIndex + 1;
            }
            while (selectOptions[indexToFocus]?.isDisabled) {
                indexToFocus++;
                if (indexToFocus === selectOptions.length) {
                    indexToFocus = 0;
                }
            }
        }

        setActiveAndFocusedItem(indexToFocus);
    };

    const onInputKeyDown = (event) => {
        const focusedItem = focusedItemIndex !== null ? selectOptions[focusedItemIndex] : null;

        switch (event.key) {
            case 'Enter':
                event.preventDefault();
                if (isOpen && focusedItem && !focusedItem.isDisabled) {
                    handleSelect(focusedItem.value);
                } else if (isOpen && isCreatable && inputValue && validateCreateValue(inputValue)) {
                    // Create new entry when Enter is pressed and input is valid
                    handleSelect(CREATE_NEW);
                } else if (!isOpen) {
                    openMenu();
                }
                break;

            case 'ArrowUp':
            case 'ArrowDown':
                event.preventDefault();
                handleMenuArrowKeys(event.key);
                break;

            case 'Escape':
                event.preventDefault();
                closeMenu();
                setInputValue('');
                break;

            case 'Tab':
                closeMenu();
                break;
        }
    };

    const onToggleClick = () => {
        if (isOpen) {
            closeMenu();
        } else {
            openMenu();
        }
        textInputRef?.current?.focus();
    };

    const onClearButtonClick = () => {
        if (onClear) {
            onClear();
        } else if (isMulti) {
            onSelect(null, []);
        } else {
            onSelect(null, '');
        }
        setInputValue('');
        resetActiveAndFocusedItem();
        textInputRef?.current?.focus();
    };

    const getChildren = (value) => {
        const option = normalizeOptions(options).find(opt => opt.value === value);
        return option?.children || value;
    };

    // Get validation status
    const getStatus = () => {
        switch (validated) {
            case 'error': return 'danger';
            case 'warning': return 'warning';
            case 'success': return 'success';
            default: return undefined;
        }
    };

    // Get validation message for create operations
    const getCreateValidationMessage = (value) => {
        if (!validateCreate) return null;

        const result = validateCreate(value);
        if (typeof result === 'object' && result.message) {
            return result.message;
        }

        return null;
    };

    const currentSelections = normalizeSelected(selected);
    const displayValue = !isMulti && selected && !inputValue ? selected : inputValue;

    const toggle = (toggleRef) => (
        <MenuToggle
            variant="typeahead"
            aria-label={ariaLabel}
            onClick={onToggleClick}
            innerRef={toggleRef}
            isExpanded={isOpen}
            isFullWidth
            isDisabled={isDisabled}
            status={getStatus()}
        >
            <TextInputGroup isPlain>
                <TextInputGroupMain
                    value={displayValue}
                    onClick={onInputClick}
                    onChange={onTextInputChange}
                    onKeyDown={onInputKeyDown}
                    id="typeahead-select-input"
                    autoComplete="off"
                    innerRef={textInputRef}
                    placeholder={(isMulti ? (Array.isArray(currentSelections) ? currentSelections.length === 0 : true) : !currentSelections) ? placeholder : ''}
                    {...(activeItemId && { 'aria-activedescendant': activeItemId })}
                    role="combobox"
                    isExpanded={isOpen}
                    aria-controls="typeahead-select-listbox"
                >
                    {isMulti && Array.isArray(currentSelections) && currentSelections.length > 0 && (
                        <LabelGroup aria-label="Current selections">
                            {currentSelections.map((selection, index) => (
                                <Label
                                    key={index}
                                    variant="outline"
                                    onClose={(ev) => {
                                        ev.stopPropagation();
                                        handleSelect(selection);
                                    }}
                                    textMaxWidth="32ch"
                                >
                                    {getChildren(selection)}
                                </Label>
                            ))}
                        </LabelGroup>
                    )}
                </TextInputGroupMain>
                <TextInputGroupUtilities
                    {...((isMulti ? (Array.isArray(currentSelections) ? currentSelections.length === 0 : true) : !selected) && !inputValue ? { style: { display: 'none' } } : {})}
                >
                    <Button
                        variant="plain"
                        onClick={onClearButtonClick}
                        aria-label="Clear input value"
                        icon={<TimesIcon />}
                        isDisabled={isDisabled}
                    />
                </TextInputGroupUtilities>
            </TextInputGroup>
        </MenuToggle>
    );

    return (
        <Select
            id="typeahead-select"
            isOpen={isOpen}
            selected={isMulti ? currentSelections : selected}
            onSelect={(_event, selection) => handleSelect(selection)}
            onOpenChange={(nextOpen) => {
                if (!nextOpen) {
                    closeMenu();
                }
            }}
            toggle={toggle}
            variant="typeahead"
            className={className}
        >
            <SelectList isAriaMultiselectable={isMulti} id="typeahead-select-listbox">
                {selectOptions.length === 0 && !isCreatable ? (
                    <SelectOption key="no-results" isDisabled>
                        {noResultsText}
                    </SelectOption>
                ) : (
                    selectOptions.map((option, index) => {
                        const isSelected = isMulti
                            ? (Array.isArray(currentSelections) && currentSelections.includes(option.value))
                            : selected === option.value;

                        return (
                            <SelectOption
                                key={option.value}
                                isFocused={focusedItemIndex === index}
                                className={option.className}
                                id={createItemId(option.value)}
                                value={option.value}
                                isSelected={isSelected}
                                hasCheckbox={isMulti && hasCheckbox}
                                ref={null}
                            >
                                {option.children}
                            </SelectOption>
                        );
                    })
                )}
            </SelectList>
        </Select>
    );
};

TypeaheadSelect.propTypes = {
    selected: PropTypes.oneOfType([
        PropTypes.string,
        PropTypes.arrayOf(PropTypes.string)
    ]),
    onSelect: PropTypes.func.isRequired,
    onClear: PropTypes.func,
    options: PropTypes.arrayOf(
        PropTypes.oneOfType([
            PropTypes.string,
            PropTypes.shape({
                value: PropTypes.string.isRequired,
                label: PropTypes.string,
                description: PropTypes.string
            })
        ])
    ),
    isMulti: PropTypes.bool,
    isCreatable: PropTypes.bool,
    onCreateOption: PropTypes.func,
    validateCreate: PropTypes.func,
    hasCheckbox: PropTypes.bool,
    placeholder: PropTypes.string,
    noResultsText: PropTypes.string,
    isDisabled: PropTypes.bool,
    validated: PropTypes.oneOf(['default', 'error', 'warning', 'success']),
    validationMessage: PropTypes.string,
    ariaLabel: PropTypes.string,
    className: PropTypes.string,
    onToggle: PropTypes.func,
    isOpen: PropTypes.bool,
    allowCustomValues: PropTypes.bool,
    openOnClick: PropTypes.bool,
    maxOptionsForClickOpen: PropTypes.number,
};

export default TypeaheadSelect;
