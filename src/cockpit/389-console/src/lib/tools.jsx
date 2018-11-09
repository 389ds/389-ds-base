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
