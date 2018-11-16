var attr_btn_html =
  '<div class="dropdown">' +
    '<button class="btn btn-default dropdown-toggle" type="button" data-toggle="dropdown">' +
      '  Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" id="attr-action-btn" aria-labelledby="attr-action-btn">' +
      '<li role=""><a role="menuitem" tabindex="0" class="attr-edit-btn" href="#schema-tab">Edit Attribute</a></li>' +
      '<li role=""><a role="menuitem" tabindex="1" class="attr-del-btn" href="#schema-tab">Delete Attribute</a></li>' +
     '</ul>' +
  '</div>';

var attr_btn_html_only_view =
  '<button class="btn btn-default attr-view-btn" type="button"' +
  ' href="#schema-tab" title="Only user-defined attributes can be modified"> View Attribute' +
  '</button>';

var oc_btn_html =
  '<div class="dropdown">' +
    '<button class="btn btn-default dropdown-toggle" type="button"data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu">' +
      '<li role=""><a role="menuitem" tabindex="0" class="oc-edit-btn" href="#schema-tab">Edit Objectclass</a></li>' +
      '<li role=""><a role="menuitem" tabindex="1" class="oc-del-btn" href="#schema-tab">Delete Objectclass</a></li>' +
    '</ul>' +
  '</div>';

var oc_btn_html_only_view =
  '<button class="btn btn-default oc-view-btn" type="button"' +
  ' href="#schema-tab" title="Only user-defined objectClasses can be modified"> View Objectclass' +
  '</button>';

var schema_oc_table;
var schema_at_table;
var schema_mr_table;

var attr_usage_opts = ['userApplications', 'directoryOperation', 'distributedOperation', 'dSAOperation'];
var oc_kind_opts = ['STRUCTURAL', 'ABSTRACT', 'AUXILIARY'];

function is_x_origin_user_defined(x_origin) {
  if (typeof x_origin === 'string' && x_origin.toLowerCase() !== 'user defined' || x_origin == null) {
    return false;
  } else {
    return true;
  }
}

// Leave only user defined attributes if the checkbox is crossed
$.fn.dataTable.ext.search.push(
  function(settings, searchData, index, rowData, counter) {
    var x_origin;
    if ( $("#attr-user-defined").is(":checked") ) {
      x_origin = rowData[10];
      if (!is_x_origin_user_defined(x_origin)) {
        return false;
      }
    }
    if ( $("#oc-user-defined").is(":checked") ) {
      x_origin = rowData[6];
      if (!is_x_origin_user_defined(x_origin)) {
        return false;
      }
    }
    return true;
  }
);

function clear_oc_form() {
  // Clear input fields and reset dropboxes
  $("#add-edit-oc-header").html('Add Objectclass');
  $(".ds-modal-error").hide();
  $("#oc-name").attr('disabled', false);
  $("#oc-name").val("");
  $(".ds-input").css("border-color", "initial");
  $("#oc-oid").val("");
  $("#oc-kind").prop('selectedIndex',0);
  $("#oc-desc").val("");
  $("#oc-parent").prop('selectedIndex',0);
  $("#schema-list").prop('selectedIndex',-1);
  $('#oc-required-list').find('option').remove();
  $('#oc-allowed-list').find('option').remove();
  $("#save-oc-button").attr('disabled', false);
}

function clear_attr_form() {
  // Clear input fields and reset dropboxes
  $("#add-edit-attr-header").html('Add Attribute');
  $(".ds-modal-error").hide();
  $("#attr-name").attr('disabled', false);
  $("#attr-name").val("");
  $(".ds-input").css("border-color", "initial");
  $("#attr-syntax").val("");
  $("#attr-desc").val("");
  $("#attr-parent").prop('selectedIndex',0);
  $("#attr-usage").prop('selectedIndex',0);
  $("#attr-oid").val("");
  $("#attr-alias").val("");
  $('#attr-multivalued').prop('checked', false);
  $('#attr-no-user-mod').prop('checked', false);
  $("#attr-eq-mr-select").prop('selectedIndex',0);
  $("#attr-order-mr-select").prop('selectedIndex',0);
  $("#attr-sub-mr-select").prop('selectedIndex',0);
  $("#save-attr-button").attr('disabled', false);
}

function load_schema_objects_to_select(object, select_id) {
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', object, 'list'];
  log_cmd('load_schema_objects_to_select', 'Get schema', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(select_data) {
    var obj = JSON.parse(select_data);
    var data = [];
    for (var i = 0; i < obj.items.length; i++) {
      item = obj.items[i];
      if (item.name) {
        data.push.apply(data, [item.name]);
      } else {
        data.push.apply(data, [item.oid]);
      }
    }
    // Update html select
    $.each(data, function (i, item) {
        $("#" + select_id).append($('<option>', {
            value: item,
            text : item
        }));
    });
  }).fail(function(select_data) {
      console.log("Get schema failed: " + select_data.message);
      check_inst_alive(1);
  });
}

function get_and_set_schema_tables() {
  console.log("Loading schema...");

  // Set attribute usage select html in attribute's edit window
  $.each(attr_usage_opts, function (i, item) {
      $("#attr-usage").append($('<option>', {
          value: item,
          text : item,
      }));
  });

  // Set objectClass kind select html in objectClass's edit window
  $.each(oc_kind_opts, function (i, item) {
      $("#oc-kind").append($('<option>', {
          value: item,
          text : item,
      }));
  });

  // Setup the tables: standard, custom, and Matching Rules
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'objectclasses', 'list'];
  log_cmd('get_and_set_schema_tables', 'Get objectclasses', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(oc_data) {
    var obj = JSON.parse(oc_data);
    var data = [];
    // If objectClass is user defined them the action button is enabled
    for (var i = 0; i < obj.items.length; i++) {
      var oc_btn = oc_btn_html_only_view;
      item = obj.items[i];
      if (is_x_origin_user_defined(item.x_origin)) {
        oc_btn = oc_btn_html;
      }
      // Validate all params
      if (item.oid === undefined) {
         item.oid = "";
      }
      if (item.sup === undefined) {
         item.sup = "";
      }
      if (item.must === undefined) {
         item.must = [];
      }
      if (item.may === undefined) {
         item.may = [];
      }
      if (item.x_origin === undefined) {
         item.x_origin = "";
      }
      if (item.kind === undefined) {
         item.kind = "";
      }
      if (item.desc === undefined) {
         item.desc = "";
      }

      data.push.apply(data, [[
        item.name,
        item.oid,
        item.sup,
        item.must.join(" "),
        item.may.join(" "),
        oc_btn,
        item.x_origin,
        oc_kind_opts[item.kind],
        item.desc
      ]]);
    }
    // Update html table
    schema_oc_table = $('#oc-table').DataTable ({
      "data": data,
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
      "language": {
        "emptyTable": "No objectclasses defined",
        "search": "Search Objectclasses"
      },
      "columnDefs": [ {
        "targets": 5,
        "orderable": false
      }, {
        "targets": 6,
        "visible": false
      }]
    });
  }).fail(function(oc_data) {
      console.log("Get objectclasses failed: " + oc_data.message);
      check_inst_alive(1);
  });

  // Get syntaxes and use the data to populate the attribute's table
  log_cmd('get_and_set_schema_tables', 'Get syntaxes', cmd);
  cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', "attributetypes", 'get_syntaxes'];
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(syntax_data) {
    var obj = JSON.parse(syntax_data);
    var syntax_list = [];

    load_schema_objects_to_select('matchingrules', 'attr-eq-mr-select');
    load_schema_objects_to_select('matchingrules', 'attr-order-mr-select');
    load_schema_objects_to_select('matchingrules', 'attr-sub-mr-select');
    load_schema_objects_to_select('attributetypes', 'schema-list');
    load_schema_objects_to_select('objectclasses', 'oc-parent');
    load_schema_objects_to_select('attributetypes', 'attr-parent');

    for (var i = 0; i < obj.items.length; i++) {
      item = obj.items[i];
      syntax_list.push.apply(syntax_list, [item]);
    }
    // Update syntax select html in attribute's edit window
    $.each(syntax_list, function (i, item) {
        $("#attr-syntax").append($('<option>', {
            value: item.id,
            text : item.name + " (" + item.id + ")"
        }));
    });

    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'attributetypes', 'list'];
    log_cmd('get_and_set_schema_tables', 'Get attributes', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(at_data) {
      var obj = JSON.parse(at_data);
      var data = [];
      var syntax_name = "";
      for (var i = 0; i < obj.items.length; i++) {
        var attr_btn = attr_btn_html_only_view;
        item = obj.items[i];
        if (item.single_value) {
            multivalued = 'no';
        } else {
            multivalued = 'yes';
        }
        $.each(syntax_list, function (i, syntax) {
          if (syntax.id === item.syntax) {
            syntax_name = '<div title="' + syntax.id + '">' + syntax.name + '</div>';
          }
        });
        // If attribute is user defined them the action button is enabled
        if (is_x_origin_user_defined(item.x_origin)) {
          attr_btn = attr_btn_html;
        }
        // Validate all params
        if (item.oid === undefined) {
           item.oid = "";
        }
        if (item.sup === undefined) {
           item.sup = "";
        }
        if (item.equality === undefined) {
           item.equality = "";
        }
        if (item.ordering === undefined) {
           item.ordering = "";
        }
        if (item.x_origin === undefined) {
           item.x_origin = "";
        }
        if (item.substr === undefined) {
           item.substr = "";
        }
        if (item.no_user_mod === undefined) {
           item.no_user_mod = "";
        }
        if (item.desc === undefined) {
           item.desc = "";
        }
        if (item.usage === undefined) {
           item.usage = "";
        }
        if (item.aliases === undefined) {
           item.aliases = "";
        }

        data.push.apply(data, [[
          item.name,
          item.oid,
          syntax_name,
          multivalued,
          item.equality,
          item.ordering,
          item.substr,
          attr_btn,
          item.desc,
          item.aliases,
          item.x_origin,
          attr_usage_opts[item.usage],
          item.no_user_mod,
          item.sup
        ]]);
      }
      // Update html table
      schema_at_table = $('#attr-table').DataTable({
        "data": data,
        "paging": true,
        "bAutoWidth": false,
        "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
        "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
        "language": {
          "emptyTable": "No attributes defined",
          "search": "Search Attributes"
        },
        "columnDefs": [ {
          "targets": 7,
          "orderable": false
        }, {
          "targets": 8,
          "visible": false
        }]
      });
    }).fail(function(at_data) {
        console.log("Get attributes failed: " + at_data.message);
        check_inst_alive(1);
    });

  }).fail(function(syntax_data) {
      console.log("Get syntaxes failed: " + syntax_data.message);
      check_inst_alive(1);
  });

  cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'matchingrules', 'list'];
  log_cmd('get_and_set_schema_tables', 'Get matching rules', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(mr_data) {
    var obj = JSON.parse(mr_data);
    var data = [];
    for (var i = 0; i < obj.items.length; i++) {
      item = obj.items[i];
      data.push.apply(data, [[
        item.name,
        item.oid,
        item.syntax,
        item.desc]]);
    }
    schema_mr_table = $('#schema-mr-table').DataTable({
      "paging": true,
      "data": data,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No matching rules defined",
        "search": "Search Matching Rules"
      },
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
    });

    console.log("Finished loading schema.");

  }).fail(function(mr_data) {
      console.log("Get matching rules failed: " + mr_data.cmd);
      check_inst_alive(1);
  });
}

$(document).ready( function() {
  // Set an interval event to wait for all the pages to load, then load the config
  $("#schema-content").load("schema.html", function (){
    // Sort schema list awhile
    sort_list( $("#schema-list") );

    $("#objectclass-btn").on("click", function() {
      $(".all-pages").hide();
      $("#schema-content").show();
      $("#objectclass-page").show();
    });
    $("#attribute-btn").on("click", function() {
      $(".all-pages").hide();
      $("#schema-content").show();
      $("#attribute-page").show();
    });
    $("#schema-mr-btn").on("click", function() {
      $(".all-pages").hide();
      $("#schema-content").show();
      $("#schema-mr").show();
    });

    $('#attr-user-defined').change(function() {
      schema_at_table.draw();
    });
    $('#oc-user-defined').change(function() {
      schema_oc_table.draw();
    });

    //
    // Modals/Forms
    //

    /*
     *
     * Add Objectclass Form
     *
     */
    $("#add-oc-button").on("click", function() {
      clear_oc_form();
      document.getElementById("oc-parent").value = 'top';
    });

    $("#save-oc-button").on("click", function() {
      var oc_name = $("#oc-name").val();
      var oc_oid = $("#oc-oid").val();
      var oc_parent = $("#oc-parent").val();
      var oc_kind = $("#oc-kind").val();
      var oc_desc = $("#oc-desc").val();
      var oc_x_origin = $("#oc-x-origin").val();
      var oc_required_list = $('#oc-required-list option').map(function() { return $(this).val(); }).get();
      var oc_allowed_list = $('#oc-allowed-list option').map(function() { return $(this).val(); }).get();

      var action = 'add';
      var edit = false;
      if ( $("#add-edit-oc-header").text().indexOf("Edit Objectclass") != -1){
        edit = true;
        action = 'edit';
      }
      if (oc_name == '') {
        report_err($("#oc-name"), 'You must provide an objectClass name');
        return;
      }
      var cmd = [DSCONF, server_inst, 'schema', 'objectclasses', action, oc_name];
      // Process and validate parameters
      cmd.push.apply(cmd, ["--oid", oc_oid]);
      cmd.push.apply(cmd, ["--sup", oc_parent]);
      cmd.push.apply(cmd, ["--kind", oc_kind]);
      cmd.push.apply(cmd, ["--desc", oc_desc]);
      cmd.push.apply(cmd, ["--x-origin", oc_x_origin]);
      cmd.push.apply(cmd, ["--must"]);
      if (oc_required_list.length !== 0) {
        cmd.push.apply(cmd, oc_required_list);
      } else {
        cmd.push.apply(cmd, [""]);
      }
      cmd.push.apply(cmd, ["--may"]);
      if (oc_allowed_list.length !== 0) {
        cmd.push.apply(cmd, oc_allowed_list);
      } else {
        cmd.push.apply(cmd, [""]);
      }

      $("#save-oc-spinner").show();
      log_cmd('#save-oc-button (click)', 'Save objectclasses', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#oc-name").attr('disabled', false);

        // Update html table (if edit: delete old then add new)
        if ( edit ) {
          var selector = $('tr:contains(' + oc_name + ')');
          schema_oc_table.row(selector).remove().draw(false);
        }
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'objectclasses', 'query', oc_name];
        log_cmd('#save-oc-button (click)', 'Search objectclasses', cmd);
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
        done(function(oc_data) {
          var obj = JSON.parse(oc_data);
          var item = obj.oc;
          schema_oc_table.row.add( [
            item.name,
            item.oid,
            item.sup,
            item.must.join(" "),
            item.may.join(" "),
            oc_btn_html,
            item.x_origin,
            oc_kind_opts[item.kind],
            item.desc
          ] ).draw( false );
        }).
        fail(function(oc_data) {
          popup_err("err", oc_data.message);
          console.log("Search objectclasses failed: " + oc_data.message);
          check_inst_alive(1);
        });
        // Replace the option in 'Edit objectClass' window
        if (!edit) {
          var option = $('<option></option>').attr("value", oc_name).text(oc_name);
          $("#oc-parent").append(option);
        }
        $("#save-oc-spinner").hide();
        popup_success("The objectClass was saved in DS");
        $("#add-edit-oc-form").modal('toggle');
      }).
      fail(function(data) {
        $("#save-oc-spinner").hide();
        popup_err("Error", "Failed to save the objectClass\n" + data.message);
        $("#add-edit-oc-form").modal('toggle');
      });
    });

    // Required Attributes
    $("#oc-must-add-button").on("click", function () {
      var add_attrs = $("#schema-list").val();
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#oc-required-list option[value="' + add_attrs[i] + '"]').val() === undefined) {
            $('#oc-required-list').append($("<option/>").val(add_attrs[i]).text(add_attrs[i]));
          }
        }
        $("#schema-list").find('option:selected').remove();
      }
    });
    $("#oc-must-remove-button").on("click", function () {
      var add_attrs = $("#oc-required-list").find('option:selected');
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#schema-list option[value="' + add_attrs[i].text + '"]').val() === undefined) {
            $('#schema-list').append($("<option/>").val(add_attrs[i].text).text(add_attrs[i].text));
          }
        }
      }
      $("#oc-required-list").find('option:selected').remove();
      sort_list( $("#schema-list") );
    });

    // Allowed Attributes
    $("#oc-may-add-button").on("click", function () {
      var add_attrs = $("#schema-list").val();
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#oc-allowed-list option[value="' + add_attrs[i] + '"]').val() === undefined) {
            $('#oc-allowed-list').append($("<option/>").val(add_attrs[i]).text(add_attrs[i]));
          }
        }
        $("#schema-list").find('option:selected').remove();
      }
    });
    $("#oc-may-remove-button").on("click", function () {
      var add_attrs = $("#oc-allowed-list").find('option:selected');
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#schema-list option[value="' + add_attrs[i].text + '"]').val() === undefined) {
            $('#schema-list').append($("<option/>").val(add_attrs[i].text).text(add_attrs[i].text));
          }
        }
      }
      $("#oc-allowed-list").find('option:selected').remove();
      sort_list( $("#schema-list") );
    });

    /*
     *
     * Add Attribute Form
     *
     */
    $("#create-attr-button").on("click", function() {
      clear_attr_form();
    });

    $("#save-attr-button").on("click", function() {
      var attr_name = $("#attr-name").val();
      var attr_oid = $("#attr-oid").val();
      var attr_syntax = $("#attr-syntax").val();
      var attr_syntax_text = $("#attr-syntax :selected").text();
      var attr_usage = $('#attr-usage').val();
      var attr_desc = $('#attr-desc').val();
      var attr_x_origin= $('#attr-x-origin').val();
      var attr_parent = $('#attr-parent').val();
      var attr_aliases = $('#attr-alias').val().split(" ");
      var eq_mr= $('#attr-eq-mr-select').val();
      var order_mr = $('#attr-order-mr-select').val();
      var sub_mr  = $('#attr-sub-mr-select').val();
      var multiple = 'no';
      if ( $("#attr-multivalued").is(":checked") ) {
        multiple = 'yes';
      }
      var no_user_mod = false;
      if ( $("#attr-no-user-mod").is(":checked") ) {
        no_user_mod = true;
      }
      var action = 'add';
      var edit = false;
      if ( $("#add-edit-attr-header").text().indexOf("Edit Attribute") != -1){
        edit = true;
        action = 'edit';
      }

      if (attr_name == '') {
        report_err($("#attr-name"), 'You must provide an attribute name');
        return;
      }
      if (attr_syntax == '') {
        report_err($("#attr-syntax"), 'You must provide an attribute syntax');
        return;
      }

      var cmd = [DSCONF, server_inst, 'schema', 'attributetypes', action, attr_name];
      // Process and validate parameters
      if (attr_aliases) {
        cmd.push.apply(cmd, ["--aliases"]);
        cmd.push.apply(cmd, attr_aliases);
      }
      if (attr_syntax) {
        cmd.push.apply(cmd, ["--syntax", attr_syntax]);
      }
      if (multiple == 'no') {
        cmd.push.apply(cmd, ["--single-value"]);
      } else {
        cmd.push.apply(cmd, ["--multi-value"]);
      }
      if (no_user_mod) {
        cmd.push.apply(cmd, ["--no-user-mod"]);
      } else {
        cmd.push.apply(cmd, ["--user-mod"]);
      }
      cmd.push.apply(cmd, ["--oid", attr_oid]);
      cmd.push.apply(cmd, ["--usage", attr_usage]);
      cmd.push.apply(cmd, ["--sup", attr_parent]);
      cmd.push.apply(cmd, ["--desc", attr_desc]);
      cmd.push.apply(cmd, ["--x-origin", attr_x_origin]);
      cmd.push.apply(cmd, ["--equality"]);
      if (eq_mr) {
        cmd.push.apply(cmd, [eq_mr]);
      } else {
        cmd.push.apply(cmd, [""]);
      }
      cmd.push.apply(cmd, ["--substr"]);
      if (sub_mr) {
        cmd.push.apply(cmd, [sub_mr]);
      } else {
        cmd.push.apply(cmd, [""]);
      }
      cmd.push.apply(cmd, ["--ordering"]);
      if (order_mr) {
        cmd.push.apply(cmd, [order_mr]);
      } else {
        cmd.push.apply(cmd, [""]);
      }
      $("#save-attr-spinner").show();
      log_cmd('#save-attr-button (click)', 'Save attribute', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        var attr_syntax_name = '<div title="' + attr_syntax + '">' +
                               attr_syntax_text.substr(0, attr_syntax_text.indexOf(" (")) + '</div>';
        $("#attr-name").attr('disabled', false);
        // Update html table (if edit: delete old then add new)
        if ( edit ) {
          var selector = $('tr:contains(' + attr_name + ')');
          schema_at_table.row(selector).remove().draw(false);
        }
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'attributetypes', 'query', attr_name];
        log_cmd('#save-oc-button (click)', 'Get attribute', cmd);
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
        done(function(at_data) {
          var obj = JSON.parse(at_data);
          var item = obj.at;
          schema_at_table.row.add( [
            item.name,
            item.oid,
            attr_syntax_name,
            multiple,
            item.equality,
            item.ordering,
            item.substr,
            attr_btn_html,
            item.desc,
            item.aliases,
            item.x_origin,
            attr_usage_opts[item.usage],
            item.no_user_mod,
            item.sup
          ] ).draw( false );
          $("#attr-name").attr('disabled', false);
        }).
        fail(function(at_data) {
          popup_err("err", at_data.message);
          console.log("Query attributes failed: " + at_data.message);
          check_inst_alive(1);
        });
        if (!edit) {
          var option = $('<option></option>').attr("value", attr_name).text(attr_name);
          $("#schema-list").append(option);
        }
        $("#save-attr-spinner").hide();
        popup_success("The attribute was saved in DS");
        $("#add-edit-attr-form").modal('toggle');
      }).
      fail(function(data) {
        $("#save-attr-spinner").hide();
        popup_err("Error", "Failed to save the attribute\n" + data.message);
        $("#add-edit-attr-form").modal('toggle');
     });
    });

    function load_attr_form(element) {
      clear_attr_form();
      var data = schema_at_table.row(element.parents('tr') ).data();
      var edit_attr_name = data[0];
      var edit_attr_oid = data[1];
      var edit_attr_syntax = $.parseHTML(data[2])[0].title;
      var edit_attr_multivalued = data[3];
      var edit_attr_eq_mr = data[4];
      var edit_attr_order_mr = data[5];
      var edit_attr_sub_mr = data[6];
      var edit_attr_desc = data[8];
      var edit_attr_aliases = data[9];
      var edit_attr_x_origin = data[10];
      var edit_attr_usage = data[11];
      var edit_attr_no_user_mod = data[12];
      var edit_attr_parent = data[13];
      if (edit_attr_eq_mr) {
        edit_attr_eq_mr = data[4];
      }
      if (edit_attr_order_mr) {
        edit_attr_order_mr = data[5];
      }
      if (edit_attr_sub_mr) {
        edit_attr_sub_mr = data[6];
      }

      $("#add-edit-attr-header").html('Edit Attribute: ' + edit_attr_name);
      $("#attr-name").val(edit_attr_name);
      $("#attr-name").attr('disabled', true);
      $("#attr-oid").val(edit_attr_oid);
      $("#attr-usage")[0].value = edit_attr_usage;
      $("#attr-parent")[0].value = edit_attr_parent;
      $("#attr-desc").val(edit_attr_desc);
      $("#attr-x-origin").val(edit_attr_x_origin);
      if (edit_attr_aliases) {
        $("#attr-alias").val(edit_attr_aliases.join(" "));
      }
      $("#attr-syntax").val(edit_attr_syntax);
      $("#attr-multivalued").prop('checked', false);
      if (edit_attr_multivalued == "yes") {
        $("#attr-multivalued").prop('checked', true);
      }
      $("#attr-no-user-mod").prop('checked', false);
      if (edit_attr_no_user_mod) {
        $("#attr-no-user-mod").prop('checked', true);
      }
      $("#save-attr-spinner").show();
      $("#attr-eq-mr-select")[0].value = edit_attr_eq_mr;
      $("#attr-order-mr-select")[0].value = edit_attr_order_mr;
      $("#attr-sub-mr-select")[0].value = edit_attr_sub_mr;
      $("#save-attr-spinner").hide();

      $("#add-edit-attr-form").modal('toggle');
    }

    $(document).on('click', '.attr-view-btn', function(e) {
      e.preventDefault();
      load_attr_form($(this));
      $("#save-attr-button").attr('title', 'Only user-defined attributes can be modified');
      $("#save-attr-button").attr('disabled', true);
    });

    $(document).on('click', '.attr-edit-btn', function(e) {
      e.preventDefault();
      load_attr_form($(this));
    });

    $(document).on('click', '.attr-del-btn', function(e) {
      e.preventDefault();
      var data = schema_at_table.row( $(this).parents('tr') ).data();
      var del_attr_name = data[0];
      var at_row = $(this);
      popup_confirm("Are you sure you want to delete attribute: <b>" + del_attr_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'attributetypes', 'remove', del_attr_name];
          log_cmd('.attr-del-btn (click)', 'Remove attribute', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success("Attribute was successfully removed!");
            schema_at_table.row( at_row.parents('tr') ).remove().draw( false );
            $("#schema-list option[value='" + del_attr_name + "']").remove();
          }).fail(function(data) {
            popup_err("Attribute removal error", del_attr_name + " removal has failed: " + data.message);
          });
        }
      });
    });

    function load_oc_form(element) {
      clear_oc_form();
      var data = schema_oc_table.row(element.parents('tr') ).data();
      var edit_oc_name = data[0];
      var edit_oc_oid = data[1];
      var edit_oc_parent = data[2];
      var edit_oc_required = data[3].split(" ");
      var edit_oc_allowed = data[4].split(" ");
      var edit_oc_x_origin = data[6];
      var edit_oc_kind = data[7];
      var edit_oc_desc = data[8];
        if (edit_oc_parent) {
          edit_oc_parent = data[2];
        }

      $("#save-oc-spinner").show();
      $("#add-edit-oc-header").html('Edit Objectclass: ' + edit_oc_name);
      $("#oc-name").attr('disabled', true);
      $("#oc-name").val(edit_oc_name);
      $("#oc-oid").val(edit_oc_oid);
      $("#oc-kind")[0].value = edit_oc_kind;
      $("#oc-desc").val(edit_oc_desc);
      $("#oc-x-origin").val(edit_oc_x_origin);
      $("#oc-parent")[0].value = edit_oc_parent;
      $.each(edit_oc_required, function (i, item) {
        if (item) {
          $("#oc-required-list").append($('<option>', {
            value: item,
            text : item
          }));
        }
      });
      $.each(edit_oc_allowed, function (i, item) {
        if (item) {
          $("#oc-allowed-list").append($('<option>', {
            value: item,
            text : item
          }));
        }
      });
      $("#save-oc-spinner").hide();

      // Update modal html header and fields and show()
      $("#add-edit-oc-form").modal('toggle');
    }

    $(document).on('click', '.oc-view-btn', function(e) {
      e.preventDefault();
      load_oc_form($(this));
      $("#save-oc-button").attr('title', 'Only user-defined objectClasses can be modified');
      $("#save-oc-button").attr('disabled', true);
    });

    $(document).on('click', '.oc-edit-btn', function(e) {
      e.preventDefault();
      load_oc_form($(this));
    });

    $(document).on('click', '.oc-del-btn', function(e) {
      e.preventDefault();
      var data = schema_oc_table.row( $(this).parents('tr') ).data();
      var del_oc_name = data[0];
      var oc_row = $(this);

      popup_confirm("Are you sure you want to delete objectclass: <b>" + del_oc_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'objectclasses', 'remove', del_oc_name];
          log_cmd('.oc-del-btn (click)', 'Remove objectclass', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success("ObjectClass was successfully removed!");
            schema_oc_table.row( oc_row.parents('tr') ).remove().draw( false );
            $("#oc-parent option[value='" + del_oc_name + "']").remove();
          }).fail(function(data) {
            popup_err("Error", del_oc_name + " removal has failed: " + data.message);
          });
        }
      });
    });
    // Page is loaded, mark it as so...
    schema_page_loaded = 1;
  });
});


