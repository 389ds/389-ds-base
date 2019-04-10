export function searchFilter(searchFilterValue, columnsToSearch, rows) {
    if (searchFilterValue && rows && rows.length) {
        const filteredRows = [];
        rows.forEach(row => {
            var rowToSearch = [];
            if (columnsToSearch && columnsToSearch.length) {
                columnsToSearch.forEach(column =>
                    rowToSearch.push(row[column])
                );
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
        var pw_args = ["--passwd", "--bind-pw"];
        var cmd_list = [];
        var converted_pw = false;

        for (var idx in cmd_array) {
            var cmd = cmd_array[idx];
            converted_pw = false;
            for (var arg_idx in pw_args) {
                if (cmd.startsWith(pw_args[arg_idx])) {
                    // We are setting a password, if it has a value we need to hide it
                    var arg_len = cmd.indexOf("=");
                    var arg = cmd.substring(0, arg_len);
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
        console.log(
            "CMD: " + js_func + ": " + desc + " ==> " + cmd_list.join(" ")
        );
    }
}

// Convert DS timestamp to a friendly string: 20180921142257Z -> 10/21/2018, 2:22:57 PM
export function get_date_string (timestamp) {
    let year = timestamp.substr(0, 4);
    let month = timestamp.substr(4, 2);
    let day = timestamp.substr(6, 2);
    let hour = timestamp.substr(8, 2);
    let minute = timestamp.substr(10, 2);
    let sec = timestamp.substr(12, 2);
    let date = new Date(parseInt(year), parseInt(month), parseInt(day),
                        parseInt(hour), parseInt(minute), parseInt(sec));
    return date.toLocaleString();
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
    let startDate = new Date(parseInt(year), parseInt(month), parseInt(day),
                             parseInt(hour), parseInt(minute), parseInt(sec));

    // Get the servers current date
    year = end.substr(0, 4);
    month = end.substr(4, 2);
    day = end.substr(6, 2);
    hour = end.substr(8, 2);
    minute = end.substr(10, 2);
    sec = end.substr(12, 2);
    let currDate = new Date(parseInt(year), parseInt(month), parseInt(day),
                            parseInt(hour), parseInt(minute), parseInt(sec));

    // Generate pretty elapsed time string
    let seconds = Math.floor((currDate - (startDate)) / 1000);
    let minutes = Math.floor(seconds / 60);
    let hours = Math.floor(minutes / 60);
    let days = Math.floor(hours / 24);
    hours = hours - (days * 24);
    minutes = minutes - (days * 24 * 60) - (hours * 60);
    seconds = seconds - (days * 24 * 60 * 60) - (hours * 60 * 60) - (minutes * 60);

    return `${days} days, ${hours} hours, ${minutes} minutes, and ${seconds} seconds`;
}
