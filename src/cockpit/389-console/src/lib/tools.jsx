import cockpit from "cockpit";

export function searchFilter(searchFilterValue, columnsToSearch, rows) {
    if (searchFilterValue && rows && rows.length) {
        const filteredRows = [];
        rows.forEach(row => {
            let rowToSearch = [];
            if (columnsToSearch && columnsToSearch.length) {
                columnsToSearch.forEach(column => {
                    if (column in row) {
                        if (row[column] != null) {
                            rowToSearch.push(row[column]);
                        }
                    }
                });
            } else {
                rowToSearch = row;
            }
            const match = Object.values(rowToSearch).some(value =>
                value
                        .join()
                        .toLowerCase()
                        .includes(searchFilterValue.toLowerCase())
            );
            if (match) {
                filteredRows.push(row);
            }
        });
        return filteredRows;
    }
    return rows;
}

export function log_cmd(js_func, desc, cmd_array) {
    if (console) {
        const pw_args = ["--passwd", "--bind-pw", "--nsslapd-rootpw"];
        let cmd_list = [];
        let converted_pw = false;

        for (const idx in cmd_array) {
            const cmd = cmd_array[idx].toString();
            converted_pw = false;
            for (const arg_idx in pw_args) {
                if (cmd.startsWith(pw_args[arg_idx])) {
                    // We are setting a password, if it has a value we need to hide it
                    const arg_len = cmd.indexOf("=");
                    const arg = cmd.substring(0, arg_len);
                    if (cmd.length != arg_len + 1) {
                        // We are setting a password value...
                        cmd_list.push(arg + "=**********");
                        converted_pw = true;
                    }
                    break;
                }
            }
            if (!converted_pw) {
                cmd_list.push(cmd);
            }
        }
        console.log("CMD: " + js_func + ": " + desc + " ==> " + cmd_list.join(" "));
    }
}

// Convert DS timestamp to a friendly string: 20180921142257Z -> 10/21/2018, 2:22:57 PM
export function get_date_string(timestamp) {
    const year = timestamp.substr(0, 4);
    const month = timestamp.substr(4, 2);
    const day = timestamp.substr(6, 2);
    const hour = timestamp.substr(8, 2);
    const minute = timestamp.substr(10, 2);
    const sec = timestamp.substr(12, 2);
    const date = new Date(
        parseInt(year),
        parseInt(month) - 1,
        parseInt(day),
        parseInt(hour),
        parseInt(minute),
        parseInt(sec)
    );
    return date.toLocaleString('en-ZA');
}

// Take two directory server tiemstamps and get the elapsed time
export function get_date_diff(start, end) {
    // Get the server's start up date
    let year = start.substr(0, 4);
    let month = start.substr(4, 2);
    let day = start.substr(6, 2);
    let hour = start.substr(8, 2);
    let minute = start.substr(10, 2);
    let sec = start.substr(12, 2);
    const startDate = new Date(
        parseInt(year),
        parseInt(month),
        parseInt(day),
        parseInt(hour),
        parseInt(minute),
        parseInt(sec)
    );

    // Get the servers current date
    year = end.substr(0, 4);
    month = end.substr(4, 2);
    day = end.substr(6, 2);
    hour = end.substr(8, 2);
    minute = end.substr(10, 2);
    sec = end.substr(12, 2);
    const currDate = new Date(
        parseInt(year),
        parseInt(month),
        parseInt(day),
        parseInt(hour),
        parseInt(minute),
        parseInt(sec)
    );

    // Generate pretty elapsed time string
    let seconds = Math.floor((currDate - startDate) / 1000);
    let minutes = Math.floor(seconds / 60);
    let hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    hours = hours - days * 24;
    minutes = minutes - days * 24 * 60 - hours * 60;
    seconds = seconds - days * 24 * 60 * 60 - hours * 60 * 60 - minutes * 60;

    // Handle plurals
    if (days == "1") {
        day = "day";
    } else {
        day = "days";
    }
    if (hours == "1") {
        hour = "hour";
    } else {
        hour = "hours";
    }
    if (minutes == "1") {
        minute = "minute";
    } else {
        minute = "minutes";
    }
    if (seconds == "1") {
        sec = "second";
    } else {
        sec = "seconds";
    }

    return `${days} ${day}, ${hours} ${hour}, ${minutes} ${minute}, and ${seconds} ${sec}`;
}

export function bad_file_name(file_name) {
    // file_name must be a string, and not a location/directory
    if (file_name.includes("/")) {
        return true;
    }
    return false;
}

export function file_is_path(file_name) {
    if (file_name.length >= 2 && file_name.startsWith("/") && !file_name.endsWith("/")) {
        // Simple and Crude
        return true;
    } else {
        return false;
    }
}

export function valid_port(val) {
    // Validate value is a number and between 1 and 65535
    let result = !isNaN(val);
    if (result) {
        if (val < 1 || val > 65535) {
            result = false;
        }
    }
    return result;
}

export function valid_dn(dn) {
    // Validate value is a valid DN (sanity validation)
    if (dn == "" || dn.endsWith(",")) {
        return false;
    }
    const dn_regex = new RegExp("^([A-Za-z])+=\\S.*");
    const result = dn_regex.test(dn);
    return result;
}

export function valid_filter(filter) {
    // Validate filter is within parenthesis
    if (filter.startsWith("(") && filter.endsWith(")")) {
        return true;
    }
    return false;
}

export function numToCommas(num) {
    //  Convert a number to have human friendly commas
    return num.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",");
}

export function displayBytes(bytes) {
    // Convert bytes into a more human readable value/unit
    if (bytes === 0 || isNaN(bytes)) {
        return '0 Bytes';
    }
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));

    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

export function displayKBytes(kbytes) {
    // Convert kilobytes into a more human readable value/unit
    if (kbytes === 0 || isNaN(kbytes)) {
        return '0 KB';
    }
    const k = 1024;
    const sizes = ['KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'];
    const i = Math.floor(Math.log(kbytes) / Math.log(k));

    return parseFloat((kbytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

export function listsEqual(list1, list2) {
    if (Array.isArray(list1) && Array.isArray(list2)) {
        const list1_sorted = [...list1].sort();
        const list2_sorted = [...list2].sort();
        if (list1_sorted.length != list2_sorted.length) {
            return false;
        }
        for (let i = list1_sorted.length; i--;) {
            if (list1_sorted[i].toLowerCase() != list2_sorted[i].toLowerCase()) {
                return false;
            }
        }
        return true;
    } else if (list1 === undefined && list2 === undefined) {
        return true;
    } else {
        return false;
    }
}

export function validHostname(hostname) {
    var reHostname = new RegExp(/^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$/);
    return reHostname.exec(hostname);
}

export function callCmdStreamPassword(config) {
    // Cmd will trigger CLI to prompt for password, add it via stream
    let cmd = [...config.cmd];
    if (config.passwd !== "" && config.promptArg !== "") {
        // Add password file arg
        cmd.push(config.promptArg);
    }
    let buffer = "";

    const proc = cockpit.spawn(cmd, { pty: true, environ: ["LC_ALL=C"], superuser: true, err: "message" });
    proc
            .done(data => {
                config.addNotification("success", config.success_msg);
                config.state_callback();
                if (config.reload_func) {
                    config.reload_func(config.reload_arg);
                }
                if (config.ext_func) {
                    config.ext_func(config.ext_arg);
                }
            })
            .fail(_ => {
                config.addNotification("error", config.error_msg + ": " + buffer);
                config.state_callback();
                if (config.reload_func) {
                    config.reload_func(config.reload_arg);
                }
                if (config.ext_func) {
                    config.ext_func(config.ext_arg);
                }
            })
            .stream(data => {
                buffer += data;
                proc.input(config.passwd + "\n", true);
            });
}
