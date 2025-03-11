import React from "react";
import cockpit from "cockpit";
import { log_cmd, valid_dn } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    Alert,
    Button,
    Card,
    Checkbox,
    DatePicker,
    Form,
    FormGroup,
    Grid,
    GridItem,
    HelperText,
    HelperTextItem,
    InputGroup,
    InputGroupItem,
    isValidDate,
    NumberInput,
    Radio,
    Split,
    SplitItem,
    Text,
    TextContent,
    TextInput,
    TextVariants,
    TimePicker,
    Tooltip,
    SimpleList,
    SimpleListItem,
    Modal,
    ModalVariant,
    Bullseye,
    Spinner,
    EmptyState,
    EmptyStateIcon,
    EmptyStateBody,
    Title,
    Chip,
    ChipGroup,
    yyyyMMddFormat,
    ActionGroup
} from "@patternfly/react-core";
import {
    SyncAltIcon,
    InfoCircleIcon,
    ExclamationCircleIcon,
    FolderIcon,
    FolderOpenIcon,
    PlusCircleIcon,
    TrashIcon,
    FolderCloseIcon,
} from "@patternfly/react-icons";
import { LagReportModal, ChooseLagReportModal } from "./monitorModals.jsx";

const _ = cockpit.gettext;

// Constants for form validation
const TIME_FORMAT_REGEX = /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/;
const UTC_OFFSET_REGEX = /^[+-]\d{4}$/;

export class ReplLogAnalysis extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            // Form inputs
            logDirs: "",
            logDirsList: [],
            suffixesList: props.suffixDN ? [props.suffixDN] : [],
            currentSuffixInput: "",
            anonymizeOption: false,
            replicationFilter: "all", // 'all', 'only-fully', 'only-not'
            lagTimeLowest: "",
            etimeLowest: "",
            replLagThreshold: "",
            utcOffset: "+0000",
            formatOptions: {
                json: true,       // JSON format for PatternFly chart data
                html: true,       // HTML format - default to true
                png: true,        // PNG format - default to true
                csv: true         // CSV format - default to true
            },
            useCustomOutputDir: false,
            customOutputDir: "",
            reportName: "",

            // File browser state
            showFileBrowser: false,
            currentPath: "/var/log/dirsrv",
            directoryContents: [],
            selectedDirectories: [],
            isLoading: false,
            browsingError: null,
            fileBrowserPurpose: "logDirs", // can be "logDirs" or "outputDir"
            recentDirectories: ["/var/log/dirsrv", "/tmp"],

            // Form validation
            errors: {
                logDirs: "",
                suffixes: "",
                utcOffset: "",
                startDate: "",
                endDate: "",
                formatOptions: "",
                customOutputDir: ""
            },

            // UI state
            isGeneratingReport: false,
            showLagReportModal: false,
            reportUrls: {},
            tempDirectories: [], // Track temp dirs for cleanup
            reportError: null,
            startDate: null,
            endDate: null,
            isReplReportsPackageInstalled: false,
            showChooseLagReportModal: false
        };

        // Bind methods
        this.handleInputChange = this.handleInputChange.bind(this);
        this.handleAnonymizeChange = this.handleAnonymizeChange.bind(this);
        this.handleReplicationFilterChange = this.handleReplicationFilterChange.bind(this);
        this.handleFormatOptionChange = this.handleFormatOptionChange.bind(this);
        this.handleSubmit = this.handleSubmit.bind(this);
        this.generateReport = this.generateReport.bind(this);
        this.saveReport = this.saveReport.bind(this);
        this.closeLagReportModal = this.closeLagReportModal.bind(this);
        this.validateForm = this.validateForm.bind(this);
        
        this.openFileBrowser = this.openFileBrowser.bind(this);
        this.closeFileBrowser = this.closeFileBrowser.bind(this);
        this.browseDirectory = this.browseDirectory.bind(this);
        this.addDirectory = this.addDirectory.bind(this);
        this.removeDirectory = this.removeDirectory.bind(this);
        this.navigateUp = this.navigateUp.bind(this);
        
        this.toggleDirectorySelection = this.toggleDirectorySelection.bind(this);
        this.addSelectedDirectories = this.addSelectedDirectories.bind(this);
        this.selectAllDirectories = this.selectAllDirectories.bind(this);
        
        this.handleSuffixInputChange = this.handleSuffixInputChange.bind(this);
        this.addSuffix = this.addSuffix.bind(this);
        this.removeSuffix = this.removeSuffix.bind(this);
        this.handleSuffixKeyPress = this.handleSuffixKeyPress.bind(this);
        
        this.handleNumberInputChange = this.handleNumberInputChange.bind(this);
        this.handleNumberInputMinus = this.handleNumberInputMinus.bind(this);
        this.handleNumberInputPlus = this.handleNumberInputPlus.bind(this);
        
        this.handleUtcOffsetChange = this.handleUtcOffsetChange.bind(this);
        this.handleUtcOffsetMinus = this.handleUtcOffsetMinus.bind(this);
        this.handleUtcOffsetPlus = this.handleUtcOffsetPlus.bind(this);
        this.handleStartDateChange = this.handleStartDateChange.bind(this);
        this.handleStartTimeChange = this.handleStartTimeChange.bind(this);
        this.handleEndDateChange = this.handleEndDateChange.bind(this);
        this.handleEndTimeChange = this.handleEndTimeChange.bind(this);
        this.formatDateTimeForCommand = this.formatDateTimeForCommand.bind(this);
        this.handleUseCustomOutputDirChange = this.handleUseCustomOutputDirChange.bind(this);
        this.selectOutputDirectory = this.selectOutputDirectory.bind(this);
        this.setCustomOutputDir = this.setCustomOutputDir.bind(this);
        this.handlePathInputChange = this.handlePathInputChange.bind(this);
        this.handlePathInputKeyPress = this.handlePathInputKeyPress.bind(this);
        this.handleCustomOutputDirChange = this.handleCustomOutputDirChange.bind(this);
        this.handleReportNameChange = this.handleReportNameChange.bind(this);
        this.openChooseLagReportModal = this.openChooseLagReportModal.bind(this);
        this.closeChooseLagReportModal = this.closeChooseLagReportModal.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
        this.validateDirectoryExists("/var/log/dirsrv");
        this.checkReplReportsPackage();
        
        // Prepopulate suffixes list with replicatedSuffixes if available
        if (this.props.replicatedSuffixes && this.props.replicatedSuffixes.length > 0) {
            this.setState({
                suffixesList: [...this.state.suffixesList, ...this.props.replicatedSuffixes]
            });
        }
    }

    validateDirectoryExists(path) {
        return new Promise((resolve, reject) => {
            if (!path || path.trim() === '') {
                resolve(false);
                return;
            }
            
            cockpit
                .spawn(["test", "-d", path], { superuser: true, err: "message" })
                .then(() => resolve(true))
                .catch(() => resolve(false));
        });
    }

    checkReplReportsPackage() {
        // Check if python3-lib389-repl-reports package is installed
        cockpit.spawn(["rpm", "-q", "python3-lib389-repl-reports"], { err: "ignore" })
            .then(() => {
                // Package is installed
                this.setState({ isReplReportsPackageInstalled: true });
            })
            .catch(() => {
                // Package is not installed
                this.setState(prevState => ({
                    isReplReportsPackageInstalled: false,
                    // Disable HTML and PNG format options if package is not installed
                    formatOptions: {
                        ...prevState.formatOptions,
                        html: false,
                        png: false
                    }
                }));
            });
    }

    handleInputChange(event, value) {
        let id = event.target.id;
        const errors = { ...this.state.errors };

        // Clear previous error for this field
        errors[id] = "";

        // Validate based on field type
        if (id === "utcOffset" && value && !UTC_OFFSET_REGEX.test(value)) {
            errors.utcOffset = _("UTC offset must be in ±HHMM format (e.g., -0400, +0530)");
        }

        if ((id === "startDate" || id === "endDate") && value && !TIME_FORMAT_REGEX.test(value)) {
            errors[id] = _("Date format must be YYYY-MM-DD HH:MM:SS");
        }

        if ((id === "lagTimeLowest" || id === "etimeLowest" || id === "replLagThreshold") && value) {
            const numValue = parseFloat(value);
            if (isNaN(numValue) || numValue < 0) {
                errors[id] = _("Must be a positive number");
            }
        }

        this.setState({
            [id]: value,
            errors
        });
    }

    handleAnonymizeChange(checked) {
        this.setState({
            anonymizeOption: checked
        });
    }

    handleReplicationFilterChange(value) {
        this.setState({
            replicationFilter: value
        });
    }

    handleFormatOptionChange(format) {
        // For HTML and PNG formats, check if the package is installed
        if ((format === 'html' || format === 'png') && !this.state.isReplReportsPackageInstalled) {
            // Don't allow toggling these formats if the package is not installed
            return;
        }
        
        this.setState(prevState => ({
            formatOptions: {
                ...prevState.formatOptions,
                [format]: !prevState.formatOptions[format]
            }
        }));
    }

    handleUseCustomOutputDirChange(checked) {
        this.setState({
            useCustomOutputDir: checked,
            // Reset custom directory if turning off the option
            customOutputDir: checked ? this.state.customOutputDir : "",
            errors: {
                ...this.state.errors,
                customOutputDir: ""
            }
        });
    }

    handleCustomOutputDirChange(event) {
        const value = event.target.value;
        this.setState({
            customOutputDir: value,
            errors: {
                ...this.state.errors,
                customOutputDir: ""
            }
        });
    }

    handleReportNameChange(event) {
        this.setState({
            reportName: event.target.value
        });
    }

    handleSuffixInputChange(event, value) {
        this.setState({
            currentSuffixInput: value,
            errors: {
                ...this.state.errors,
                suffixes: valid_dn(value) || value === "" ? "" : "Invalid DN format" // Validate and clear/set error
            }
        });
    }

    addSuffix() {
        const { suffixesList, currentSuffixInput } = this.state;
        const trimmedSuffix = currentSuffixInput.trim();
        
        if (!trimmedSuffix) {
            return; // Empty input, do nothing
        }
        
        if (!valid_dn(trimmedSuffix)) {
            this.setState({
                errors: {
                    ...this.state.errors,
                    suffixes: "Invalid DN format"
                }
            });
            return;
        }
        
        if (!suffixesList.includes(trimmedSuffix)) {
            const newList = [...suffixesList, trimmedSuffix];
            
            this.setState({
                suffixesList: newList,
                currentSuffixInput: "", // Clear the input field
                errors: {
                    ...this.state.errors,
                    suffixes: "" // Clear any previous error
                }
            });
        }
    }

    handleSuffixKeyPress(event) {
        if (event.key === 'Enter') {
            event.preventDefault();
            this.addSuffix();
        }
    }

    removeSuffix(index) {
        const { suffixesList } = this.state;
        const newList = [...suffixesList];
        newList.splice(index, 1);
        
        this.setState({
            suffixesList: newList,
            errors: {
                ...this.state.errors,
                suffixes: newList.length === 0 ? _("At least one suffix is required") : ""
            }
        });
    }

    async validateForm() {
        const { logDirsList, suffixesList, utcOffset, startDate, endDate, formatOptions, useCustomOutputDir, customOutputDir } = this.state;
        const errors = { ...this.state.errors };
        let isValid = true;

        // Required fields
        if (logDirsList.length === 0) {
            errors.logDirs = _("At least one log directory is required");
            isValid = false;
        } else {
            errors.logDirs = "";
        }

        if (suffixesList.length === 0) {
            errors.suffixes = _("At least one suffix is required");
            isValid = false;
        } else {
            errors.suffixes = "";
        }

        // Check that at least one format option is selected
        if (!Object.values(formatOptions).some(selected => selected)) {
            errors.formatOptions = _("At least one report format must be selected");
            isValid = false;
        } else {
            errors.formatOptions = "";
        }

        // Custom output directory validation
        if (useCustomOutputDir) {
            if (!customOutputDir || customOutputDir.trim() === '') {
                errors.customOutputDir = _("Output directory is required when using custom location");
                isValid = false;
            } else {
                // Check if directory exists
                const dirExists = await this.validateDirectoryExists(customOutputDir);
                if (!dirExists) {
                    errors.customOutputDir = _("Directory does not exist or is not accessible");
                    isValid = false;
                } else {
                    errors.customOutputDir = "";
                }
            }
        }

        // Format validation
        if (utcOffset && !UTC_OFFSET_REGEX.test(utcOffset)) {
            errors.utcOffset = _("UTC offset must be in ±HHMM format (e.g., -0400, +0530)");
            isValid = false;
        }

        // If start date and end date are both specified, check that start is before end
        if (startDate && endDate && startDate > endDate) {
            errors.endDate = _("End time must be after start time");
            isValid = false;
        }

        this.setState({ errors });
        return isValid;
    }

    handleSubmit(e) {
        e.preventDefault();
        this.validateForm().then(isValid => {
            if (isValid) {
                this.generateReport();
            }
        });
    }

    generateReport() {
        this.setState({
            isGeneratingReport: true,
            reportError: null
        });

        // Create a unique output directory or use the custom one
        let outputDir;
        let dirName;
        
        // Use custom report name if provided, otherwise use timestamp
        if (this.state.reportName.trim()) {
            dirName = this.state.reportName.trim();
        } else {
            dirName = `repl_report_${Date.now()}`;
        }
        
        if (this.state.useCustomOutputDir && this.state.customOutputDir) {
            // Create a subdirectory in the parent directory
            outputDir = `${this.state.customOutputDir}/${dirName}`;
        } else {
            outputDir = `/tmp/${dirName}`;
            // Add to tracked directories (only track temp directories)
            this.setState(prevState => ({
                tempDirectories: [...prevState.tempDirectories, outputDir]
            }));
        }

        // Prepare command parameters
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "lag-report",
            "--log-dirs"
        ];

        // Add log directories
        const { logDirsList, suffixesList } = this.state;
        logDirsList.forEach(dir => cmd.push(dir));

        // Add suffixes - in dsconf, suffixes is nargs='+' so each suffix is a separate argument
        if (suffixesList.length > 0) {
            cmd.push("--suffixes");
            // When using cockpit.spawn with an array, each array element becomes a separate argument
            suffixesList.forEach(suffix => cmd.push(suffix));
        }

        // Add output directory
        cmd.push("--output-dir", outputDir);

        // Always use --json flag for command output format (clean JSON output for UI)
        cmd.push("--json");

        // Add selected output formats
        const { formatOptions } = this.state;
        const outputFormats = [];
        
        // Only include JSON format if selected
        if (formatOptions.json) {
            outputFormats.push("json");
        }
        
        if (formatOptions.html) {
            outputFormats.push("html");
        }
        
        if (formatOptions.png) {
            outputFormats.push("png");
        }
        
        if (formatOptions.csv) {
            outputFormats.push("csv");
        }
        
        cmd.push("--output-format", ...outputFormats);

        // Add filter options
        if (this.state.anonymizeOption) {
            cmd.push("--anonymous");
        }

        // Replication filtering - only add one option
        if (this.state.replicationFilter === "only-fully") {
            cmd.push("--only-fully-replicated");
        } else if (this.state.replicationFilter === "only-not") {
            cmd.push("--only-not-replicated");
        }

        // Add thresholds
        if (this.state.lagTimeLowest) {
            cmd.push("--lag-time-lowest", this.state.lagTimeLowest);
        }

        if (this.state.etimeLowest) {
            cmd.push("--etime-lowest", this.state.etimeLowest);
        }

        if (this.state.replLagThreshold) {
            cmd.push("--repl-lag-threshold", this.state.replLagThreshold);
        }

        // Add time range using the formatted date-time values
        const startTimeStr = this.formatDateTimeForCommand(this.state.startDate);
        if (startTimeStr) {
            cmd.push("--start-time", startTimeStr);
        }

        const endTimeStr = this.formatDateTimeForCommand(this.state.endDate);
        if (endTimeStr) {
            cmd.push("--end-time", endTimeStr);
        }

        // Add UTC offset
        if (this.state.utcOffset) {
            cmd.push("--utc-offset", this.state.utcOffset);
        }

        log_cmd("generateReplReport", "Generate replication log report", cmd);

        // Execute command
        cockpit
            .spawn(cmd, { superuser: true, err: "message" })
            .done(content => {
                try {
                    let result;
                    try {
                        // Try to parse as JSON - handle case where response might contain non-JSON text
                        // Find the first occurrence of '{' to start JSON parsing from there
                        const jsonStart = content.indexOf('{');
                        if (jsonStart >= 0) {
                            const jsonContent = content.substring(jsonStart);
                            result = JSON.parse(jsonContent);
                        } else {
                            throw new Error("No JSON object found in response");
                        }
                    } catch (parseError) {
                        // If parsing fails, log and throw the error
                        console.error("JSON parse error:", parseError);
                        throw new Error("Failed to parse server response as JSON: " + parseError.message);
                    }
                    
                    // Check for a valid response structure
                    if (!result || !result.type) {
                        throw new Error("Invalid response format from server");
                    }
                    
                    if (result.type === "list") {
                        // Initialize reportUrls object
                        const reportUrls = {};
                        
                        // Handle both array and object formats for items
                        if (Array.isArray(result.items)) {
                            // Process array format
                            result.items.forEach(path => {
                                if (path.endsWith(".html")) {
                                    reportUrls.html = path;
                                } else if (path.endsWith(".png")) {
                                    reportUrls.png = path;
                                } else if (path.endsWith(".csv")) {
                                    reportUrls.csv = path;
                                } else if (path.endsWith("_summary.json")) {
                                    reportUrls.summary = path;
                                } else if (path.endsWith(".json") && !path.endsWith("_summary.json")) {
                                    reportUrls.json = path;
                                }
                            });
                        } else if (typeof result.items === 'object' && result.items !== null) {
                            // Process object format where keys are formats and values are paths
                            Object.entries(result.items).forEach(([format, path]) => {
                                if (format === 'html' || path.endsWith(".html")) {
                                    reportUrls.html = path;
                                } else if (format === 'png' || path.endsWith(".png")) {
                                    reportUrls.png = path;
                                } else if (format === 'csv' || path.endsWith(".csv")) {
                                    reportUrls.csv = path;
                                } else if (format === 'summary' || path.endsWith("_summary.json")) {
                                    reportUrls.summary = path;
                                } else if (format === 'json' || (path.endsWith(".json") && !path.endsWith("_summary.json"))) {
                                    reportUrls.json = path;
                                }
                            });
                        } else {
                            throw new Error("Invalid items format in server response");
                        }
                        
                        // Check if we have any report URLs
                        if (Object.keys(reportUrls).length > 0) {
                            this.setState({
                                isGeneratingReport: false,
                                showLagReportModal: true,
                                reportUrls
                            });
                        } else {
                            throw new Error("No report files were found in the server response");
                        }
                    } else {
                        throw new Error("Unexpected response type: " + result.type);
                    }
                } catch (e) {
                    console.error("Error processing report generation response:", e);
                    this.setState({
                        isGeneratingReport: false,
                        reportError: e.message || _("Failed to process the response from the server")
                    });
                    this.props.addNotification(
                        "error",
                        _("Failed to process the response from the server: ") + (e.message || "")
                    );
                }
            })
            .fail(err => {
                let errMsg;
                try {
                    const errorObj = JSON.parse(err);
                    errMsg = errorObj.desc || errorObj.message || err.toString();
                } catch (e) {
                    errMsg = err.toString();
                }

                console.error("Report generation error:", errMsg);
                this.setState({
                    isGeneratingReport: false,
                    reportError: errMsg
                });

                this.props.addNotification(
                    "error",
                    cockpit.format(_("Failed to generate replication log report: $0"), errMsg)
                );
            });
    }

    closeLagReportModal() {
        this.setState({
            showLagReportModal: false,
            reportUrls: {},
        });
    }

    saveReport(format) {
        const reportUrl = this.state.reportUrls[format];
        if (!reportUrl) return;

        // Read file content
        cockpit.file(reportUrl).read()
            .then(content => {
                // Determine content type based on format
                let contentType;
                if (format === "html") {
                    contentType = "text/html";
                } else if (format === "png") {
                    contentType = "image/png";
                } else if (format === "csv") {
                    contentType = "text/csv";
                } else {
                    contentType = "text/plain";
                }

                // Create blob and download
                const blob = new Blob([content], { type: contentType });
                const url = URL.createObjectURL(blob);

                const a = document.createElement("a");
                a.href = url;
                a.download = `replication_report.${format}`;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);

                URL.revokeObjectURL(url);
            })
            .catch(error => {
                this.props.addNotification(
                    "error",
                    cockpit.format(_("Failed to save report: $0"), error.message)
                );
            });
    }

    openFileBrowser() {
        this.setState({ 
            showFileBrowser: true,
            fileBrowserPurpose: "logDirs",
            selectedDirectories: [], // Reset selected directories when opening browser
            currentPath: "/var/log/dirsrv" // Always reset to the default log directory path
        }, () => {
            this.browseDirectory("/var/log/dirsrv"); // Browse to the default path immediately
        });
    }

    closeFileBrowser() {
        this.setState({ 
            showFileBrowser: false,
            browsingError: null
        });
    }

    browseDirectory(path) {
        this.setState({ 
            isLoading: true,
            browsingError: null,
            selectedDirectories: [] // Reset selections when changing directories
        });

        cockpit
            .spawn(["find", path, "-maxdepth", "1", "-type", "d", "-not", "-path", "*/\\.*"], { superuser: true, err: "message" })
            .then(output => {
                const dirs = output.trim().split('\n')
                    .filter(dir => dir !== path) // Remove the current directory from the list
                    .sort();
                
                // Update current path and add to recent directories if not already there
                this.setState(prevState => {
                    // Add to recent directories if not already in list
                    let recentDirs = [...prevState.recentDirectories];
                    if (!recentDirs.includes(path)) {
                        recentDirs.unshift(path); // Add to beginning
                        recentDirs = recentDirs.slice(0, 5); // Keep only 5 most recent
                    }
                    
                    return {
                        currentPath: path,
                        directoryContents: dirs,
                        isLoading: false,
                        recentDirectories: recentDirs
                    };
                });
            })
            .catch(error => {
                console.error("Error browsing directory:", error);
                this.setState({
                    isLoading: false,
                    browsingError: _("Failed to browse directory: ") + (error.message || error)
                });
            });
    }

    navigateUp() {
        const parentPath = this.state.currentPath.split('/').slice(0, -1).join('/');
        // Ensure we don't navigate above root
        if (parentPath) {
            this.browseDirectory(parentPath);
        } else {
            this.browseDirectory('/');
        }
    }

    handlePathInputChange(event) {
        this.setState({
            currentPath: event.target.value
        });
    }

    handlePathInputKeyPress(event) {
        if (event.key === 'Enter') {
            this.browseDirectory(this.state.currentPath);
        }
    }

    addDirectory(path) {
        if (this.state.fileBrowserPurpose === "logDirs") {
            const { logDirsList } = this.state;
            
            // Check if directory is already in the list
            if (!logDirsList.includes(path)) {
                const newList = [...logDirsList, path];
                
                this.setState({
                    logDirsList: newList,
                    logDirs: newList.join('|'),
                    errors: {
                        ...this.state.errors,
                        logDirs: "" // Clear any previous error
                    }
                });
            }
        } else if (this.state.fileBrowserPurpose === "outputDir") {
            // Set selected directory as output directory
            this.setCustomOutputDir(path);
        }
        
        this.closeFileBrowser();
    }

    removeDirectory(index) {
        const { logDirsList } = this.state;
        const newList = [...logDirsList];
        newList.splice(index, 1);
        
        this.setState({
            logDirsList: newList,
            logDirs: newList.join('|'),
            errors: {
                ...this.state.errors,
                logDirs: newList.length === 0 ? _("At least one log directory is required") : ""
            }
        });
    }

    toggleDirectorySelection(dir, isChecked) {
        this.setState(prevState => {
            if (isChecked) {
                // When selecting directories for output, only allow one selection
                if (prevState.fileBrowserPurpose === "outputDir") {
                    return { selectedDirectories: [dir] };
                }
                
                // For log directories, add to selected directories if not already there
                if (!prevState.selectedDirectories.includes(dir)) {
                    return { selectedDirectories: [...prevState.selectedDirectories, dir] };
                }
            } else {
                // Remove from selected directories
                return { 
                    selectedDirectories: prevState.selectedDirectories.filter(d => d !== dir) 
                };
            }
            return null; // No change needed
        });
    }

    selectAllDirectories(isChecked) {
        // Don't allow selecting all for output directories
        if (this.state.fileBrowserPurpose === "outputDir") {
            return;
        }
        
        if (isChecked) {
            this.setState({ selectedDirectories: [...this.state.directoryContents] });
        } else {
            this.setState({ selectedDirectories: [] });
        }
    }

    addSelectedDirectories() {
        if (this.state.selectedDirectories.length === 0) {
            return;
        }

        if (this.state.fileBrowserPurpose === "logDirs") {
            // Add to log directories
            this.setState(prevState => ({
                logDirsList: [...new Set([...prevState.logDirsList, ...prevState.selectedDirectories])],
                showFileBrowser: false,
                errors: {
                    ...prevState.errors,
                    logDirs: "" // Clear error if there was one
                }
            }));
        } else if (this.state.fileBrowserPurpose === "outputDir" && this.state.selectedDirectories.length > 0) {
            // Set as output directory
            this.setCustomOutputDir(this.state.selectedDirectories[0]);
        }
    }

    handleNumberInputChange(id, event) {
        const value = event.target.value;
        const errors = { ...this.state.errors };
        
        // Clear previous error for this field
        errors[id] = "";
        
        // Validate the input
        if (value !== "") {
            const numValue = parseFloat(value);
            if (isNaN(numValue) || numValue < 0) {
                errors[id] = _("Must be a positive number");
            }
        }
        
        this.setState({
            [id]: value === "" ? value : parseFloat(value),
            errors
        });
    }
    
    handleNumberInputMinus(id) {
        const currentValue = this.state[id];
        
        // If empty or not a number, start from 0
        const numValue = (currentValue === "" || isNaN(parseFloat(currentValue))) ? 0 : parseFloat(currentValue);
        
        // Don't go below 0
        if (numValue > 0) {
            const newValue = Math.max(0, numValue - 1);
            this.setState({
                [id]: newValue,
                errors: {
                    ...this.state.errors,
                    [id]: ""
                }
            });
        }
    }
    
    handleNumberInputPlus(id) {
        const currentValue = this.state[id];
        
        // If empty or not a number, start from 0
        const numValue = (currentValue === "" || isNaN(parseFloat(currentValue))) ? 0 : parseFloat(currentValue);
        
        const newValue = numValue + 1;
        this.setState({
            [id]: newValue,
            errors: {
                ...this.state.errors,
                [id]: ""
            }
        });
    }

    formatUtcOffset(minutes) {
        // Convert minutes to hours and remaining minutes
        const absMinutes = Math.abs(minutes);
        const hours = Math.floor(absMinutes / 60);
        const mins = absMinutes % 60;
        
        // Format with sign, hours (2 digits), and minutes (2 digits)
        const sign = minutes < 0 ? "-" : "+";
        return `${sign}${hours.toString().padStart(2, '0')}${mins.toString().padStart(2, '0')}`;
    }
    
    parseUtcOffset(offsetStr) {
        if (!offsetStr || !UTC_OFFSET_REGEX.test(offsetStr)) {
            return 0; // Default to UTC
        }
        
        const sign = offsetStr.charAt(0) === '-' ? -1 : 1;
        const hours = parseInt(offsetStr.substring(1, 3), 10);
        const minutes = parseInt(offsetStr.substring(3, 5), 10);
        
        return sign * (hours * 60 + minutes);
    }
    
    handleUtcOffsetChange(event, value) {
        const errors = { ...this.state.errors };
        
        // Clear previous error
        errors.utcOffset = "";
        
        // If input is empty, allow it
        if (!value) {
            this.setState({
                utcOffset: "",
                errors
            });
            return;
        }
        
        // Check if it matches the expected format
        if (UTC_OFFSET_REGEX.test(value)) {
            this.setState({
                utcOffset: value,
                errors
            });
            return;
        }
        
        // If it doesn't match the format, show error
        errors.utcOffset = _("UTC offset must be in ±HHMM format (e.g., -0400, +0530)");
        this.setState({ 
            utcOffset: value,
            errors 
        });
    }
    
    handleUtcOffsetMinus() {
        const { utcOffset } = this.state;
        const currentMinutes = this.parseUtcOffset(utcOffset || "+0000");
        const newMinutes = Math.max(currentMinutes - 30, -1439); // Don't go below -23:59
        
        this.setState({
            utcOffset: this.formatUtcOffset(newMinutes),
            errors: {
                ...this.state.errors,
                utcOffset: ""
            }
        });
    }
    
    handleUtcOffsetPlus() {
        const { utcOffset } = this.state;
        const currentMinutes = this.parseUtcOffset(utcOffset || "+0000");
        const newMinutes = Math.min(currentMinutes + 30, 1439); // Don't go above +23:59
        
        this.setState({
            utcOffset: this.formatUtcOffset(newMinutes),
            errors: {
                ...this.state.errors,
                utcOffset: ""
            }
        });
    }

    handleStartDateChange(event, inputDate, newDate) {
        const errors = { ...this.state.errors };
        errors.startDate = "";

        if (isValidDate(this.state.startDate) && isValidDate(newDate) && inputDate === yyyyMMddFormat(newDate)) {
            // Preserve time when changing date
            newDate.setHours(this.state.startDate.getHours());
            newDate.setMinutes(this.state.startDate.getMinutes());
            newDate.setSeconds(this.state.startDate.getSeconds());
        }

        if (isValidDate(newDate) && inputDate === yyyyMMddFormat(newDate)) {
            // Check if end date is before start date
            if (this.state.endDate && newDate > this.state.endDate) {
                errors.startDate = _("Start date cannot be after end date");
            }

            this.setState({
                startDate: new Date(newDate),
                errors
            });
        } else if (!inputDate) {
            // Clear the date if input is empty
            this.setState({
                startDate: null,
                errors
            });
        }
    }

    handleStartTimeChange(event, time, hour, minute, second) {
        const { startDate } = this.state;
        const errors = { ...this.state.errors };
        errors.startDate = "";

        if (isValidDate(startDate)) {
            const updatedDate = new Date(startDate);
            updatedDate.setHours(hour || 0);
            updatedDate.setMinutes(minute || 0);
            updatedDate.setSeconds(second || 0);

            // Check if end date is before start date
            if (this.state.endDate && updatedDate > this.state.endDate) {
                errors.startDate = _("Start time cannot be after end time");
            }

            this.setState({
                startDate: updatedDate,
                errors
            });
        }
    }

    handleEndDateChange(event, inputDate, newDate) {
        const errors = { ...this.state.errors };
        errors.endDate = "";

        if (isValidDate(this.state.endDate) && isValidDate(newDate) && inputDate === yyyyMMddFormat(newDate)) {
            // Preserve time when changing date
            newDate.setHours(this.state.endDate.getHours());
            newDate.setMinutes(this.state.endDate.getMinutes());
            newDate.setSeconds(this.state.endDate.getSeconds());
        }

        if (isValidDate(newDate) && inputDate === yyyyMMddFormat(newDate)) {
            // Check if end date is before start date
            if (this.state.startDate && newDate < this.state.startDate) {
                errors.endDate = _("End date cannot be before start date");
            }

            this.setState({
                endDate: new Date(newDate),
                errors
            });
        } else if (!inputDate) {
            // Clear the date if input is empty
            this.setState({
                endDate: null,
                errors
            });
        }
    }

    handleEndTimeChange(event, time, hour, minute, second) {
        const { endDate } = this.state;
        const errors = { ...this.state.errors };
        errors.endDate = "";

        if (isValidDate(endDate)) {
            const updatedDate = new Date(endDate);
            updatedDate.setHours(hour || 0);
            updatedDate.setMinutes(minute || 0);
            updatedDate.setSeconds(second || 0);

            // Check if end date is before start date
            if (this.state.startDate && updatedDate < this.state.startDate) {
                errors.endDate = _("End time cannot be before start time");
            }

            this.setState({
                endDate: updatedDate,
                errors
            });
        }
    }

    formatDateTimeForCommand(date) {
        if (!date || !isValidDate(date)) return "";
        
        const year = date.getFullYear();
        const month = String(date.getMonth() + 1).padStart(2, '0');
        const day = String(date.getDate()).padStart(2, '0');
        const hours = String(date.getHours()).padStart(2, '0');
        const minutes = String(date.getMinutes()).padStart(2, '0');
        const seconds = String(date.getSeconds()).padStart(2, '0');
        
        return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
    }

    selectOutputDirectory() {
        this.setState({ 
            showFileBrowser: true,
            fileBrowserPurpose: "outputDir",
            selectedDirectories: [],
            currentPath: "/tmp" // Always reset to the default tmp directory for output
        }, () => {
            this.browseDirectory("/tmp"); // Browse to the default output directory path immediately
        });
    }

    setCustomOutputDir(dir) {
        this.setState({
            customOutputDir: dir,
            showFileBrowser: false
        });
    }

    openChooseLagReportModal() {
        this.setState({ showChooseLagReportModal: true });
    }

    closeChooseLagReportModal() {
        this.setState({ showChooseLagReportModal: false });
    }

    render() {
        const {
            logDirsList,
            suffixesList,
            currentSuffixInput,
            anonymizeOption,
            replicationFilter,
            formatOptions,
            startDate,
            endDate,
            errors,
            isGeneratingReport,
            showLagReportModal,
            reportUrls,
            reportError,
            showFileBrowser,
            currentPath,
            directoryContents,
            isLoading,
            browsingError,
            selectedDirectories
        } = this.state;

        const formIsValid = logDirsList.length > 0 && suffixesList.length > 0 &&
            Object.values(formatOptions).some(selected => selected) &&
            Object.values(errors).every(err => err === "");

        // Render the directory list items
        const renderDirectoryList = () => {
            if (logDirsList.length === 0) {
                return (
                    <EmptyState>
                        <EmptyStateIcon icon={FolderCloseIcon} />
                        <Title headingLevel="h4" size="md">
                            {_("No log directories selected")}
                        </Title>
                        <EmptyStateBody>
                            {_("Click 'Add Directory' to browse and select log directories")}
                        </EmptyStateBody>
                    </EmptyState>
                );
            }

            return (
                <SimpleList aria-label="Log directories list">
                    {logDirsList.map((dir, index) => (
                        <SimpleListItem
                            key={index}
                            className="ds-margin-top-sm"
                        >
                            <Split hasGutter>
                                <SplitItem isFilled>
                                    <FolderIcon className="ds-margin-right-sm" /> {dir}
                                </SplitItem>
                                <SplitItem>
                                    <span
                                        role="button"
                                        className="ds-delete-button"
                                        aria-label={_("Remove directory")}
                                        onClick={() => this.removeDirectory(index)}
                                        tabIndex={0}
                                        onKeyDown={(e) => {
                                            if (e.key === 'Enter' || e.key === ' ') {
                                                this.removeDirectory(index);
                                            }
                                        }}
                                        style={{ cursor: 'pointer', display: 'inline-flex' }}
                                    >
                                        <TrashIcon />
                                    </span>
                                </SplitItem>
                            </Split>
                        </SimpleListItem>
                    ))}
                </SimpleList>
            );
        };

        return (
            <div>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Replication Log Analysis")}
                            <Button
                                variant="plain"
                                aria-label={_("Refresh")}
                                onClick={this.props.handleReload}
                            >
                                <SyncAltIcon />
                            </Button>
                        </Text>
                    </TextContent>
                </div>

                <div className="ds-indent ds-margin-top-lg">
                    <Card>
                        <Form isHorizontal autoComplete="off" onSubmit={this.handleSubmit}>
                            {/* Sources and Filters section */}
                            <Title headingLevel="h4" size="md" className="ds-margin-bottom-md">
                                {_("Sources and Filters")}
                            </Title>
                            
                            <Grid>
                                {/* Log Directories - Left Column */}
                                <GridItem span={6}>
                                    <Grid title={_("Specify directories containing server access logs for analysis")}>
                                        <GridItem className="ds-label" span={4}>
                                            {_("Log Directories")} <span className="ds-required">*</span>
                                        </GridItem>
                                        <GridItem span={8}>
                                            <div className="ds-margin-bottom-md">
                                                <Button 
                                                    variant="secondary" 
                                                    icon={<PlusCircleIcon />}
                                                    onClick={this.openFileBrowser}
                                                >
                                                    {_("Add Directory")}
                                                </Button>
                                            </div>
                                            
                                            <div className="ds-margin-top-md">
                                                {renderDirectoryList()}
                                            </div>
                                            
                                            {errors.logDirs && (
                                                <HelperText>
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.logDirs}
                                                    </HelperTextItem>
                                                </HelperText>
                                            )}
                                        </GridItem>
                                    </Grid>
                                </GridItem>
                                
                                {/* Suffixes - Right Column */}
                                <GridItem span={6}>
                                    <Grid title={_("Specify the suffixes (naming contexts) to include in the analysis")}>
                                        <GridItem className="ds-label" span={4}>
                                            {_("Suffixes")} <span className="ds-required">*</span>
                                        </GridItem>
                                        <GridItem span={8}>
                                            <Split hasGutter>
                                                <SplitItem isFilled>
                                                    <TextInput
                                                        id="currentSuffixInput"
                                                        value={currentSuffixInput}
                                                        type="text"
                                                        onChange={this.handleSuffixInputChange}
                                                        onKeyDown={this.handleSuffixKeyPress}
                                                        placeholder={_("Enter a suffix (e.g., dc=example,dc=com)")}
                                                        validated={errors.suffixes ? "error" : "default"}
                                                        aria-label="Suffix input"
                                                    />
                                                </SplitItem>
                                                <SplitItem>
                                                    <Button 
                                                        variant="secondary" 
                                                        onClick={this.addSuffix}
                                                        isDisabled={!currentSuffixInput.trim()}
                                                    >
                                                        {_("Add")}
                                                    </Button>
                                                </SplitItem>
                                            </Split>
                                            
                                            {suffixesList.length > 0 && (
                                                <div className="ds-margin-top">
                                                    <ChipGroup categoryName={_("Selected Suffixes")}>
                                                        {suffixesList.map((suffix, index) => (
                                                            <Chip 
                                                                key={index} 
                                                                onClick={() => this.removeSuffix(index)}
                                                            >
                                                                {suffix}
                                                            </Chip>
                                                        ))}
                                                    </ChipGroup>
                                                </div>
                                            )}
                                            
                                            <HelperText>
                                                {errors.suffixes ? (
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.suffixes}
                                                    </HelperTextItem>
                                                ) : (
                                                    <HelperTextItem>
                                                        {_("Add one or more suffixes to analyze")}
                                                    </HelperTextItem>
                                                )}
                                            </HelperText>
                                        </GridItem>
                                    </Grid>
                                </GridItem>
                            </Grid>

                            <Grid className="ds-margin-top">
                                {/* Display Options - Left Column */}
                                <GridItem span={6}>
                                    <Title headingLevel="h4" size="md" className="ds-margin-bottom-md">
                                        {_("Display Options")}
                                    </Title>
                                    
                                    <Grid title={_("Choose how server identities are displayed in reports")}>
                                        <GridItem className="ds-label" span={4}>
                                            {_("Server Anonymization")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <Radio
                                                id="anonymizeYes"
                                                name="anonymizeOption"
                                                label={_("Anonymize server names")}
                                                isChecked={anonymizeOption}
                                                onChange={() => this.handleAnonymizeChange(true)}
                                                description={_("Replace with generic identifiers like 'server_0'")}
                                                className="ds-margin-bottom-sm"
                                            />
                                            <Radio
                                                id="anonymizeNo"
                                                name="anonymizeOption"
                                                label={_("Show actual server names")}
                                                isChecked={!anonymizeOption}
                                                onChange={() => this.handleAnonymizeChange(false)}
                                            />
                                        </GridItem>
                                    </Grid>
                                    
                                    <Grid title={_("Filter which replication entries to include in the analysis")} className="ds-margin-top">
                                        <GridItem className="ds-label" span={4}>
                                            {_("Replication Status Filter")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <Radio
                                                id="replicationFilterAll"
                                                name="replicationFilter"
                                                label={_("Show all replication entries")}
                                                isChecked={replicationFilter === "all"}
                                                onChange={() => this.handleReplicationFilterChange("all")}
                                                className="ds-margin-bottom-sm"
                                            />
                                            <Radio
                                                id="replicationFilterFullyReplicated"
                                                name="replicationFilter"
                                                label={_("Show only fully replicated entries")}
                                                isChecked={replicationFilter === "only-fully"}
                                                onChange={() => this.handleReplicationFilterChange("only-fully")}
                                                description={_("Only show entries that successfully replicated to all servers")}
                                                className="ds-margin-bottom-sm"
                                            />
                                            <Radio
                                                id="replicationFilterNotReplicated"
                                                name="replicationFilter"
                                                label={_("Show only entries that failed to replicate")}
                                                isChecked={replicationFilter === "only-not"}
                                                onChange={() => this.handleReplicationFilterChange("only-not")}
                                                description={_("Only show entries that did not replicate to all servers")}
                                            />
                                        </GridItem>
                                    </Grid>
                                </GridItem>
                                
                                {/* Time Range - Right Column */}
                                <GridItem span={6}>
                                    <Title headingLevel="h4" size="md" className="ds-margin-bottom-md">
                                        {_("Time Range")}
                                    </Title>
                                    
                                    <Grid title={_("Specify the beginning of the time period to analyze")}>
                                        <GridItem className="ds-label" span={4}>
                                            {_("Start Time")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <InputGroup>
                                                <InputGroupItem>
                                                    <DatePicker
                                                        value={startDate ? yyyyMMddFormat(startDate) : ""}
                                                        onChange={this.handleStartDateChange}
                                                        aria-label="Start date"
                                                        placeholder="YYYY-MM-DD"
                                                    />
                                                </InputGroupItem>
                                                <InputGroupItem>
                                                    <TimePicker
                                                        time={startDate ? 
                                                            `${String(startDate.getHours()).padStart(2, '0')}:${String(startDate.getMinutes()).padStart(2, '0')}:${String(startDate.getSeconds()).padStart(2, '0')}` 
                                                            : "00:00:00"}
                                                        onChange={this.handleStartTimeChange}
                                                        aria-label="Start time"
                                                        is24Hour
                                                        includeSeconds
                                                        style={{ width: '150px' }}
                                                        isDisabled={!startDate}
                                                    />
                                                </InputGroupItem>
                                            </InputGroup>
                                            <HelperText>
                                                {errors.startDate ? (
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.startDate}
                                                    </HelperTextItem>
                                                ) : (
                                                    <HelperTextItem>{_("Leave empty for beginning of logs")}</HelperTextItem>
                                                )}
                                            </HelperText>
                                        </GridItem>
                                    </Grid>
                                    
                                    <Grid title={_("Specify the end of the time period to analyze")} className="ds-margin-top">
                                        <GridItem className="ds-label" span={4}>
                                            {_("End Time")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <InputGroup>
                                                <InputGroupItem>
                                                    <DatePicker
                                                        value={endDate ? yyyyMMddFormat(endDate) : ""}
                                                        onChange={this.handleEndDateChange}
                                                        aria-label="End date"
                                                        placeholder="YYYY-MM-DD"
                                                        rangeStart={startDate}
                                                        isDisabled={!startDate}
                                                    />
                                                </InputGroupItem>
                                                <InputGroupItem>
                                                    <TimePicker
                                                        time={endDate ? 
                                                            `${String(endDate.getHours()).padStart(2, '0')}:${String(endDate.getMinutes()).padStart(2, '0')}:${String(endDate.getSeconds()).padStart(2, '0')}` 
                                                            : "23:59:59"}
                                                        onChange={this.handleEndTimeChange}
                                                        aria-label="End time"
                                                        is24Hour
                                                        includeSeconds
                                                        style={{ width: '150px' }}
                                                        isDisabled={!endDate}
                                                    />
                                                </InputGroupItem>
                                            </InputGroup>
                                            <HelperText>
                                                {errors.endDate ? (
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.endDate}
                                                    </HelperTextItem>
                                                ) : (
                                                    <HelperTextItem>{_("Leave empty for current time")}</HelperTextItem>
                                                )}
                                            </HelperText>
                                        </GridItem>
                                    </Grid>
                                    
                                    <Grid title={_("Time zone offset from UTC")} className="ds-margin-top">
                                        <GridItem className="ds-label" span={4}>
                                            {_("UTC Offset")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <Split hasGutter>
                                                <SplitItem isFilled>
                                                    <TextInput
                                                        id="utcOffset"
                                                        value={this.state.utcOffset}
                                                        type="text"
                                                        onChange={this.handleUtcOffsetChange}
                                                        placeholder="+0000"
                                                        validated={errors.utcOffset ? "error" : "default"}
                                                        aria-label="UTC offset"
                                                    />
                                                </SplitItem>
                                                <SplitItem>
                                                    <Button 
                                                        variant="control"
                                                        onClick={this.handleUtcOffsetMinus}
                                                        aria-label={_("Decrease UTC offset by 30 minutes")}
                                                    >
                                                        -
                                                    </Button>
                                                </SplitItem>
                                                <SplitItem>
                                                    <Button 
                                                        variant="control"
                                                        onClick={this.handleUtcOffsetPlus}
                                                        aria-label={_("Increase UTC offset by 30 minutes")}
                                                    >
                                                        +
                                                    </Button>
                                                </SplitItem>
                                            </Split>
                                            <HelperText>
                                                {errors.utcOffset ? (
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.utcOffset}
                                                    </HelperTextItem>
                                                ) : (
                                                    <HelperTextItem>{_("Format: ±HHMM (e.g., -0400, +0530)")}</HelperTextItem>
                                                )}
                                            </HelperText>
                                        </GridItem>
                                    </Grid>
                                </GridItem>
                            </Grid>

                            <Grid className="ds-margin-top">
                                {/* Thresholds - Left Column */}
                                <GridItem span={6}>
                                    <Title headingLevel="h4" size="md" className="ds-margin-bottom-md">
                                        {_("Thresholds")}
                                    </Title>
                                    
                                    <Grid title={_("Filter entries with lag time above this threshold")}>
                                        <GridItem className="ds-label" span={4}>
                                            {_("Lag Time (seconds)")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                id="lagTimeLowest"
                                                value={this.state.lagTimeLowest}
                                                min={0}
                                                step={0.1}
                                                onMinus={() => this.handleNumberInputMinus("lagTimeLowest")}
                                                onChange={(event) => this.handleNumberInputChange("lagTimeLowest", event)}
                                                onPlus={() => this.handleNumberInputPlus("lagTimeLowest")}
                                                inputName="lagTimeLowest"
                                                inputAriaLabel="Lag time threshold"
                                                minusBtnAriaLabel="Decrease lag time threshold"
                                                plusBtnAriaLabel="Increase lag time threshold"
                                            />
                                            {errors.lagTimeLowest && (
                                                <HelperText>
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.lagTimeLowest}
                                                    </HelperTextItem>
                                                </HelperText>
                                            )}
                                        </GridItem>
                                    </Grid>
                                    
                                    <Grid title={_("Filter entries with etime above this threshold")} className="ds-margin-top">
                                        <GridItem className="ds-label" span={4}>
                                            {_("ETime (seconds)")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                id="etimeLowest"
                                                value={this.state.etimeLowest}
                                                min={0}
                                                step={0.1}
                                                onMinus={() => this.handleNumberInputMinus("etimeLowest")}
                                                onChange={(event) => this.handleNumberInputChange("etimeLowest", event)}
                                                onPlus={() => this.handleNumberInputPlus("etimeLowest")}
                                                inputName="etimeLowest"
                                                inputAriaLabel="Etime threshold"
                                                minusBtnAriaLabel="Decrease etime threshold"
                                                plusBtnAriaLabel="Increase etime threshold"
                                            />
                                            {errors.etimeLowest && (
                                                <HelperText>
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.etimeLowest}
                                                    </HelperTextItem>
                                                </HelperText>
                                            )}
                                        </GridItem>
                                    </Grid>
                                    
                                    <Grid title={_("Threshold for highlighting in reports")} className="ds-margin-top">
                                        <GridItem className="ds-label" span={4}>
                                            {_("Replication Lag (seconds)")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <NumberInput
                                                id="replLagThreshold"
                                                value={this.state.replLagThreshold}
                                                min={0}
                                                step={0.1}
                                                onMinus={() => this.handleNumberInputMinus("replLagThreshold")}
                                                onChange={(event) => this.handleNumberInputChange("replLagThreshold", event)}
                                                onPlus={() => this.handleNumberInputPlus("replLagThreshold")}
                                                inputName="replLagThreshold"
                                                inputAriaLabel="Replication lag threshold"
                                                minusBtnAriaLabel="Decrease replication lag threshold"
                                                plusBtnAriaLabel="Increase replication lag threshold"
                                            />
                                            {errors.replLagThreshold && (
                                                <HelperText>
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.replLagThreshold}
                                                    </HelperTextItem>
                                                </HelperText>
                                            )}
                                        </GridItem>
                                    </Grid>
                                </GridItem>
                                
                                {/* Report Format - Right Column */}
                                <GridItem span={6}>
                                    <Title headingLevel="h4" size="md" className="ds-margin-bottom-md">
                                        {_("Report Format")}
                                    </Title>
                                    
                                    <Grid title={_("Select one or more output formats for the analysis report")}>
                                        <GridItem className="ds-label" span={4}>
                                            {_("Report Format")} <span className="ds-required">*</span>
                                        </GridItem>
                                        <GridItem span={8}>
                                            <FormGroup isInline label={_("Report Format")}>
                                                <Checkbox
                                                    id="formatJson"
                                                    isChecked={this.state.formatOptions.json}
                                                    isDisabled={false}
                                                    label={_("Interactive Charts and JSON")}
                                                    onChange={() => this.handleFormatOptionChange('json')}
                                                />
                                                <Tooltip
                                                    content={!this.state.isReplReportsPackageInstalled ? 
                                                        _("The python3-lib389-repl-reports package must be installed to use this format") : 
                                                        null}
                                                    trigger={this.state.isReplReportsPackageInstalled ? "manual" : "mouseenter"}
                                                >
                                                    <Checkbox
                                                        id="formatHtml"
                                                        isChecked={this.state.formatOptions.html}
                                                        isDisabled={!this.state.isReplReportsPackageInstalled}
                                                        label={_("Standalone HTML report")}
                                                        onChange={() => this.handleFormatOptionChange('html')}
                                                    />
                                                </Tooltip>
                                                <Tooltip
                                                    content={!this.state.isReplReportsPackageInstalled ? 
                                                        _("The python3-lib389-repl-reports package must be installed to use this format") : 
                                                        null}
                                                    trigger={this.state.isReplReportsPackageInstalled ? "manual" : "mouseenter"}
                                                >
                                                    <Checkbox
                                                        id="formatPng"
                                                        isChecked={this.state.formatOptions.png}
                                                        isDisabled={!this.state.isReplReportsPackageInstalled}
                                                        label={_("PNG image")}
                                                        onChange={() => this.handleFormatOptionChange('png')}
                                                    />
                                                </Tooltip>
                                                <div className="pf-v5-u-my-md"></div>  {/* PatternFly spacing utility */}
                                                <Checkbox
                                                    id="formatCsv"
                                                    isChecked={this.state.formatOptions.csv}
                                                    label={_("CSV data")}
                                                    onChange={() => this.handleFormatOptionChange('csv')}
                                                />
                                            </FormGroup>

                                            <HelperText>
                                                {errors.formatOptions ? (
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.formatOptions}
                                                    </HelperTextItem>
                                                ) : (
                                                    <>
                                                        <HelperTextItem>
                                                            {_("Select one or more report formats to generate")}
                                                        </HelperTextItem>
                                                        {!this.state.isReplReportsPackageInstalled && (
                                                            <HelperTextItem variant="indeterminate" icon={<InfoCircleIcon />}>
                                                                {_("Install the python3-lib389-repl-reports package to enable HTML and PNG report formats")}
                                                            </HelperTextItem>
                                                        )}
                                                    </>
                                                )}
                                            </HelperText>
                                        </GridItem>
                                    </Grid>

                                    <Grid className="ds-margin-top-lg">
                                        <GridItem className="ds-label" span={4}>
                                            {_("Output Location")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <Checkbox
                                                id="use-custom-output"
                                                label={_("Use custom output directory")}
                                                description={_("Choose where to save generated reports")}
                                                isChecked={this.state.useCustomOutputDir}
                                                onChange={(_, checked) => this.handleUseCustomOutputDirChange(checked)}
                                                className="ds-margin-bottom-sm"
                                            />
                                            
                                            {this.state.useCustomOutputDir && (
                                                <InputGroup>
                                                    <TextInput
                                                        id="custom-output-dir"
                                                        value={this.state.customOutputDir}
                                                        aria-label={_("Custom output directory")}
                                                        placeholder={_("Enter output directory path")}
                                                        onChange={this.handleCustomOutputDirChange}
                                                    />
                                                    <Button
                                                        variant="control"
                                                        onClick={this.selectOutputDirectory}
                                                        aria-label={_("Browse for output directory")}
                                                    >
                                                        <FolderOpenIcon />
                                                    </Button>
                                                </InputGroup>
                                            )}
                                            
                                            {this.state.useCustomOutputDir && errors.customOutputDir && (
                                                <HelperText>
                                                    <HelperTextItem variant="error" icon={<ExclamationCircleIcon />}>
                                                        {errors.customOutputDir}
                                                    </HelperTextItem>
                                                </HelperText>
                                            )}
                                            
                                            <HelperText>
                                                <HelperTextItem>
                                                    {this.state.useCustomOutputDir
                                                        ? _("Reports will be saved to subdirectories inside this parent directory")
                                                        : _("Reports will be saved to a temporary directory and can be downloaded when ready")}
                                                </HelperTextItem>
                                            </HelperText>
                                        </GridItem>
                                    </Grid>

                                    <Grid className="ds-margin-top">
                                        <GridItem className="ds-label" span={4}>
                                            {_("Report Name")}
                                        </GridItem>
                                        <GridItem span={8}>
                                            <TextInput
                                                id="report-name"
                                                value={this.state.reportName}
                                                aria-label={_("Report name")}
                                                placeholder={_("Enter custom report name (optional)")}
                                                onChange={this.handleReportNameChange}
                                            />
                                            <HelperText>
                                                <HelperTextItem>
                                                    {_("A custom name for the report. If left blank, the current timestamp will be used.")}
                                                </HelperTextItem>
                                            </HelperText>
                                        </GridItem>
                                    </Grid>
                                </GridItem>
                            </Grid>

                            <div className="ds-inline">
                                <Button
                                    variant="primary"
                                    type="submit"
                                    id="generate-report-btn"
                                    isDisabled={!formIsValid || isGeneratingReport}
                                    isLoading={isGeneratingReport}
                                    spinnerAriaValueText={isGeneratingReport ? "Generating" : undefined}
                                >
                                    {isGeneratingReport ? _("Generating...") : _("Generate Report")}
                                </Button>
                                <Button
                                    variant="secondary"
                                    id="choose-existing-report-btn"
                                    onClick={this.openChooseLagReportModal}
                                    className="ds-margin-left"
                                >
                                    {_("Choose Existing Report")}
                                </Button>
                            </div>
                            
                            {/* Empty space to push content up from bottom */}
                            <div style={{ minHeight: "40px" }}></div>
                        </Form>

                        {reportError && (
                            <Alert
                                variant="danger"
                                title={_("Error generating report")}
                                className="ds-margin-top"
                                isInline
                            >
                                {reportError}
                            </Alert>
                        )}
                    </Card>
                </div>

                {/* File Browser Modal */}
                <Modal
                    variant={ModalVariant.medium}
                    title={this.state.fileBrowserPurpose === "outputDir" 
                          ? _("Select Output Directory") 
                          : _("Select Log Directory")}
                    isOpen={showFileBrowser}
                    onClose={this.closeFileBrowser}
                    actions={[
                        <Button key="cancel" variant="link" onClick={this.closeFileBrowser}>
                            {_("Cancel")}
                        </Button>,
                        <Button 
                            key="select-current" 
                            variant="secondary" 
                            onClick={() => this.addDirectory(currentPath)}
                            isDisabled={!currentPath}
                        >
                            {this.state.fileBrowserPurpose === "outputDir"
                                ? _("Select This Directory")
                                : _("Add Current Directory")}
                        </Button>,
                        <Button 
                            key="select-multiple" 
                            variant="primary" 
                            onClick={this.addSelectedDirectories}
                            isDisabled={selectedDirectories.length === 0}
                        >
                            {this.state.fileBrowserPurpose === "outputDir"
                                ? _("Select Directory") 
                                : (selectedDirectories.length > 0 
                                    ? cockpit.format(_("Add Selected ($0)"), selectedDirectories.length) 
                                    : _("Add Selected"))}
                        </Button>
                    ]}
                >
                    <div className="ds-margin-bottom-md">
                        <Split hasGutter>
                            <SplitItem isFilled>
                                <TextInput
                                    value={currentPath}
                                    type="text"
                                    aria-label="Current path"
                                    onChange={this.handlePathInputChange}
                                    onKeyPress={this.handlePathInputKeyPress}
                                    placeholder={_("Type a directory path and press Enter or click Go")}
                                />
                            </SplitItem>
                            <SplitItem>
                                <Button
                                    variant="secondary"
                                    onClick={() => this.browseDirectory(currentPath)}
                                    aria-label={_("Go to path")}
                                >
                                    {_("Go")}
                                </Button>
                            </SplitItem>
                            <SplitItem>
                                <Button
                                    variant="secondary"
                                    onClick={this.navigateUp}
                                    aria-label={_("Navigate up")}
                                >
                                    {_("Up")}
                                </Button>
                            </SplitItem>
                        </Split>
                        <HelperText>
                            <HelperTextItem>
                                {this.state.fileBrowserPurpose === "outputDir"
                                    ? _("Select a parent directory where report subdirectories will be created")
                                    : _("Type a path directly or browse using the directory view below")}
                            </HelperTextItem>
                        </HelperText>
                    </div>

                    {isLoading ? (
                        <Bullseye>
                            <Spinner size="xl" />
                        </Bullseye>
                    ) : browsingError ? (
                        <Alert
                            variant="danger"
                            title={_("Error browsing directory")}
                            isInline
                        >
                            {browsingError}
                        </Alert>
                    ) : directoryContents.length === 0 ? (
                        <EmptyState>
                            <EmptyStateIcon icon={FolderOpenIcon} />
                            <Title headingLevel="h4" size="md">
                                {_("No subdirectories found")}
                            </Title>
                            <EmptyStateBody>
                                {_("This directory does not contain any subdirectories")}
                            </EmptyStateBody>
                        </EmptyState>
                    ) : (
                        <>
                            {/* Select All Checkbox */}
                            {this.state.fileBrowserPurpose !== "outputDir" && (
                                <div className="ds-margin-bottom-md">
                                    <Checkbox
                                        id="select-all-dirs"
                                        label={_("Select All")}
                                        isChecked={selectedDirectories.length === directoryContents.length && directoryContents.length > 0}
                                        onChange={(checked) => this.selectAllDirectories(checked)}
                                    />
                                </div>
                            )}
                            
                            <SimpleList aria-label="Directory contents">
                                {directoryContents.map((dir, index) => {
                                    const isSelected = selectedDirectories.includes(dir);
                                    const dirName = dir.split('/').pop();
                                    
                                    return (
                                        <SimpleListItem
                                            key={index}
                                            className="ds-margin-top-xs"
                                        >
                                            <Split hasGutter>
                                                <SplitItem>
                                                    <Checkbox
                                                        id={`dir-checkbox-${index}`}
                                                        aria-label={`Select ${dirName}`}
                                                        isChecked={isSelected}
                                                        onChange={(_, checked) => this.toggleDirectorySelection(dir, checked)}
                                                    />
                                                </SplitItem>
                                                <SplitItem isFilled>
                                                    <span 
                                                        className="ds-link"
                                                        onClick={() => this.browseDirectory(dir)}
                                                        role="button"
                                                        tabIndex={0}
                                                        onKeyDown={(e) => {
                                                            if (e.key === 'Enter' || e.key === ' ') {
                                                                this.browseDirectory(dir);
                                                            }
                                                        }}
                                                    >
                                                        <FolderIcon className="ds-margin-right-sm" /> {dirName}
                                                    </span>
                                                </SplitItem>
                                            </Split>
                                        </SimpleListItem>
                                    );
                                })}
                            </SimpleList>
                        </>
                    )}
                </Modal>

                {/* Report Modal */}
                <LagReportModal
                    showModal={showLagReportModal}
                    closeHandler={this.closeLagReportModal}
                    saveHandler={this.saveReport}
                    reportUrls={reportUrls}
                />

                {/* Choose Report Modal */}
                {this.state.showChooseLagReportModal && (
                    <ChooseLagReportModal
                        showing={this.state.showChooseLagReportModal}
                        onClose={this.closeChooseLagReportModal}
                        reportDirectory={this.state.customOutputDir ? this.state.customOutputDir : '/tmp'}
                    />
                )}
            </div>
        );
    }
}

ReplLogAnalysis.propTypes = {
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    enableTree: PropTypes.func,
    handleReload: PropTypes.func,
    replicatedSuffixes: PropTypes.array,
    toggleLoadingPage: PropTypes.func,
};

ReplLogAnalysis.defaultProps = {
    serverId: "",
    replicatedSuffixes: [],
    toggleLoadingPage: () => {},
};

export default ReplLogAnalysis;
