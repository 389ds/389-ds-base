import React, { useState, useRef, useEffect } from 'react';
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
    ariaLabel = "Select input",
    className,
}) => {
    const [isOpen, setIsOpen] = useState(false);
    const [inputValue, setInputValue] = useState('');
    const [selectOptions, setSelectOptions] = useState([]);
    const [focusedItemIndex, setFocusedItemIndex] = useState(null);
    const [activeItemId, setActiveItemId] = useState(null);
    const textInputRef = useRef();

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

    // Update select options based on input
    useEffect(() => {
        const normalizedOptions = normalizeOptions(options);
        const currentSelections = isMulti ? (Array.isArray(selected) ? selected : []) : [];
        let newSelectOptions = normalizedOptions;

        // Always ensure selected values are available in options, even if not in original options
        if (isMulti && currentSelections.length > 0) {
            const selectedNotInOptions = currentSelections
                .filter(selectedValue => !normalizedOptions.some(opt => opt.value === selectedValue))
                .map(selectedValue => ({ value: selectedValue, children: selectedValue }));

            newSelectOptions = [...normalizedOptions, ...selectedNotInOptions];
        }

        if (inputValue) {
            // Filter options based on input
            newSelectOptions = newSelectOptions.filter(option =>
                String(option.children).toLowerCase().includes(inputValue.toLowerCase())
            );

            // Add create option if applicable
            if (isCreatable && inputValue) {
                const isValid = validateCreate ? validateCreate(inputValue) : true;
                const alreadyExists = normalizedOptions.some(opt => opt.value === inputValue);
                const alreadySelected = isMulti ? currentSelections.includes(inputValue) : selected === inputValue;

                if (isValid && !alreadyExists && !alreadySelected) {
                    newSelectOptions = [...newSelectOptions, {
                        children: `Create "${inputValue}"`,
                        value: CREATE_NEW,
                        isCreatable: true
                    }];
                }
            }

            if (!isOpen) {
                setIsOpen(true);
            }
        }

        setSelectOptions(newSelectOptions);
        setFocusedItemIndex(null);
        setActiveItemId(null);
    }, [inputValue, options, isCreatable, validateCreate, selected, isMulti]);

    const createItemId = (value) => `select-typeahead-${value}`.replace(/\s+/g, '-');

    const setActiveAndFocusedItem = (itemIndex) => {
        setFocusedItemIndex(itemIndex);
        const focusedItem = selectOptions[itemIndex];
        if (focusedItem) {
            setActiveItemId(createItemId(focusedItem.value));
        }
    };

    const resetActiveAndFocusedItem = () => {
        setFocusedItemIndex(null);
        setActiveItemId(null);
    };

    const closeMenu = () => {
        setIsOpen(false);
        resetActiveAndFocusedItem();
    };

    const onInputClick = () => {
        if (!isOpen) {
            setIsOpen(true);
        } else if (!inputValue) {
            closeMenu();
        }
    };

    const handleSelect = (value) => {
        if (!value) return;

        if (value === CREATE_NEW) {
            if (isCreatable && inputValue && (!validateCreate || validateCreate(inputValue))) {
                if (onCreateOption) {
                    onCreateOption(inputValue);
                }

                if (isMulti) {
                    const currentSelections = Array.isArray(selected) ? selected : [];
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
                const currentSelections = Array.isArray(selected) ? selected : [];
                const newSelections = currentSelections.includes(value)
                    ? currentSelections.filter(selection => selection !== value)
                    : [...currentSelections, value];
                onSelect(null, newSelections);
                setInputValue('');
            } else {
                onSelect(null, value);
                setInputValue('');
                closeMenu();
            }
        }

        textInputRef.current?.focus();
    };

    const onTextInputChange = (_event, value) => {
        setInputValue(value);
        resetActiveAndFocusedItem();
    };

    const handleMenuArrowKeys = (key) => {
        let indexToFocus = 0;
        if (!isOpen) {
            setIsOpen(true);
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
                } else if (isOpen && isCreatable && inputValue && (!validateCreate || validateCreate(inputValue))) {
                    // Create new entry when Enter is pressed and input is valid
                    handleSelect(CREATE_NEW);
                } else if (!isOpen) {
                    setIsOpen(true);
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
        setIsOpen(!isOpen);
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

    const currentSelections = isMulti ? (Array.isArray(selected) ? selected : []) : [];
    const displayValue = !isMulti && selected && !isOpen ? selected : inputValue;

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
                    placeholder={currentSelections.length === 0 || !isMulti ? placeholder : ''}
                    {...(activeItemId && { 'aria-activedescendant': activeItemId })}
                    role="combobox"
                    isExpanded={isOpen}
                    aria-controls="typeahead-select-listbox"
                >
                    {isMulti && currentSelections.length > 0 && (
                        <LabelGroup aria-label="Current selections">
                            {currentSelections.map((selection, index) => (
                                <Label
                                    key={index}
                                    variant="outline"
                                    onClose={(ev) => {
                                        ev.stopPropagation();
                                        handleSelect(selection);
                                    }}
                                >
                                    {getChildren(selection)}
                                </Label>
                            ))}
                        </LabelGroup>
                    )}
                </TextInputGroupMain>
                <TextInputGroupUtilities
                    {...((isMulti ? currentSelections.length === 0 : !selected) && !inputValue ? { style: { display: 'none' } } : {})}
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
                            ? currentSelections.includes(option.value)
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
    ariaLabel: PropTypes.string,
    className: PropTypes.string
};

export default TypeaheadSelect;
