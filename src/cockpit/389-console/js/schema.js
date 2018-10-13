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

var oc_btn_html =
  '<div class="dropdown">' +
    '<button class="btn btn-default dropdown-toggle" type="button"data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu">' +
      '<li role=""><a role="menuitem" tabindex="0" class="oc-edit-btn" href="#">Edit Objectclass</a></li>' +
      '<li role=""><a role="menuitem" tabindex="1" class="oc-del-btn" href="#">Delete Objectclass</a></li>' +
    '</ul>' +
  '</div>';

var schema_oc_table;
var schema_at_table;
var schema_mr_table;

function clear_oc_form() {
  // Clear input fields and reset dropboxes
  $("#add-edit-oc-header").html('Add Objectclass');
  $(".ds-modal-error").hide();
  $("#oc-name").attr('disabled', false);
  $("#oc-name").val("");
  $(".ds-input").css("border-color", "initial");
  $("#oc-oid").val("");
  $("#oc-parent").prop('selectedIndex',0);
  $("#schema-list").prop('selectedIndex',-1);
  $('#oc-required-list').find('option').remove();
  $('#oc-allowed-list').find('option').remove();
};

function clear_attr_form() {
  // Clear input fields and reset dropboxes
  $("#add-edit-attr-header").html('Add Attribute');
  $(".ds-modal-error").hide();
  $("#attr-name").attr('disabled', false);
  $("#attr-name").val("");
  $(".ds-input").css("border-color", "initial");
  $("#attr-syntax").val("");
  $("#attr-desc").val("");
  $("#attr-oid").val("");
  $("#attr-alias").val("");
  $('#attr-multivalued').prop('checked', false);
  $("#attr-eq-mr-select").prop('selectedIndex',0);
  $("#attr-order-mr-select").prop('selectedIndex',0);
  $("#attr-sub-mr-select").prop('selectedIndex',0);
};

function load_schema_objects_to_select(object, select_id) {
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', object, 'list'];
  console.log("CMD: Get schema: " + cmd.join(' '));
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    var data = []
    for (var idx in obj['items']) {
      item = obj['items'][idx];
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
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive(1);
  });
}

function get_and_set_schema_tables() {
  console.log("Loading schema...");

  // Load syntaxes
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', "attributetypes", 'get_syntaxes'];
  console.log("CMD: Get syntaxes: " + cmd.join(' '));
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    var data = []

    load_schema_objects_to_select('matchingrules', 'attr-eq-mr-select')
    load_schema_objects_to_select('matchingrules', 'attr-order-mr-select')
    load_schema_objects_to_select('matchingrules', 'attr-sub-mr-select')
    load_schema_objects_to_select('attributetypes', 'schema-list')
    load_schema_objects_to_select('objectclasses', 'oc-parent')

    for (var idx in obj['items']) {
      item = obj['items'][idx];
      data.push.apply(data, [item]);
    }
    // Update html select
    $.each(data, function (i, item) {
        $("#attr-syntax").append($('<option>', {
            value: item.id,
            text : item.name + " (" + item.id + ")"
        }));
    });
    console.log("Finished loading schema.");
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive(1);
  });

  // Setup the tables: standard, custom, and Matching Rules
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'objectclasses', 'list'];
  console.log("CMD: Get objectclasses: " + cmd.join(' '));
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    var data = [];
    for (var idx in obj['items']) {
      item = obj['items'][idx];
      data.push.apply(data, [[
        item.name,
        item.oid,
        item.sup,
        item["must"].join(" "),
        item["may"].join(" "),
        oc_btn_html
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
      } ]
    });
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive(1);
  });

  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'attributetypes', 'list'];
  console.log("CMD: Get attributes: " + cmd.join(' '));
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    var data = [];
    for (var idx in obj['items']) {
      item = obj['items'][idx];
      if (item.single_value) {
          multivalued = 'no'
      } else {
          multivalued = 'yes'
      }
      data.push.apply(data, [[
        item.name,
        item.oid,
        item.syntax,
        multivalued,
        item.equality,
        item.ordering,
        item.substr,
        attr_btn_html,
        item.desc,
        item.aliases
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
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive(1);
  });

  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'matchingrules', 'list'];
  console.log("CMD: Get matching rules: " + cmd.join(' '));
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    var data = [];
    for (var idx in obj['items']) {
      item = obj['items'][idx];
      data.push.apply(data, [[
        item.name,
        item.oid,
        item.syntax,
        item.desc]])
    };
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

  }).fail(function(data) {
      console.log("failed: " + data.message);
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
      document.getElementById("oc-parent").value = 'top'
    });

    $("#save-oc-button").on("click", function() {
      var oc_name = $("#oc-name").val();
      var oc_oid = $("#oc-oid").val();
      var oc_parent = $("#oc-parent").val();
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
      cmd.push.apply(cmd, ["--must", oc_required_list]);
      cmd.push.apply(cmd, ["--may", oc_allowed_list]);

      $("#save-oc-spinner").show();
      console.log("CMD: Save objectclass: " + cmd.join(' '));
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#save-oc-spinner").hide();
        popup_success("The objectClass was saved in DS");
        $("#oc-name").attr('disabled', false);

        // Update html table (if edit: delete old then add new)
        if ( edit ) {
          var selector = $('tr:contains(' + oc_name + ')');
          schema_oc_table.row(selector).remove().draw(false);
        }
        if (oc_required_list) {
          oc_required_list = oc_required_list.join(" ")
        }
        if (oc_allowed_list) {
          oc_allowed_list = oc_allowed_list.join(" ")
        }

        schema_oc_table.row.add( [
          oc_name,
          oc_oid,
          oc_parent,
          oc_required_list,
          oc_allowed_list,
          oc_btn_html
        ] ).draw( false );
        // Replace the option in 'Edit objectClass' window
        if (!edit) {
          var option = $('<option></option>').attr("value", oc_name).text(oc_name);
          $("#oc-parent").append(option);
        }
        $("#add-edit-oc-form").modal('toggle');
      }).
      fail(function(data) {
        $("#save-oc-spinner").hide();
        popup_err("Error", "Failed to save the objectClass\n" + data.message);
        $("#add-edit-oc-form").modal('toggle');
      })
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
    })

    $("#save-attr-button").on("click", function() {
      var attr_name = $("#attr-name").val();
      var attr_oid = $("#attr-oid").val();
      var attr_syntax = $("#attr-syntax").val();
      var attr_desc = $('#attr-desc').val();
      var attr_aliases = $('#attr-alias').val().split(" ");
      var eq_mr= $('#attr-eq-mr-select').val();
      var order_mr = $('#attr-order-mr-select').val();
      var sub_mr  = $('#attr-sub-mr-select').val();
      var multiple = 'no';
      if ( $("#attr-multivalued").is(":checked") ) {
        multiple = 'yes';
      };
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
      cmd.push.apply(cmd, ["--oid", attr_oid]);
      cmd.push.apply(cmd, ["--desc", attr_desc]);
      cmd.push.apply(cmd, ["--equality", eq_mr]);
      cmd.push.apply(cmd, ["--substr", order_mr]);
      cmd.push.apply(cmd, ["--ordering", sub_mr]);
      $("#save-attr-spinner").show();
      console.log("CMD: Save attribute: " + cmd.join(' '));
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#save-attr-spinner").hide();
        popup_success("The attribute was saved in DS");
        $("#attr-name").attr('disabled', false);
        // Update html table (if edit: delete old then add new)
        if ( edit ) {
          var selector = $('tr:contains(' + attr_name + ')');
          schema_at_table.row(selector).remove().draw(false);
        }

        // Create attribute row to dataTable
        schema_at_table.row.add( [
              attr_name,
              attr_oid,
              attr_syntax,
              multiple,
              eq_mr,
              order_mr,
              sub_mr,
              attr_btn_html,
              attr_desc,
              attr_aliases
         ] ).draw( false );
         if (!edit) {
           var option = $('<option></option>').attr("value", attr_name).text(attr_name);
           $("#schema-list").append(option);
         }
        $("#add-edit-attr-form").modal('toggle');
      }).
      fail(function(data) {
        $("#save-attr-spinner").hide();
        popup_err("Error", "Failed to save the attribute\n" + data.message);
        $("#add-edit-attr-form").modal('toggle');
     })
    });

    $(document).on('click', '.attr-edit-btn', function(e) {
        e.preventDefault();
        clear_attr_form();
        var data = schema_at_table.row( $(this).parents('tr') ).data();
        var edit_attr_name = data[0];
        var edit_attr_oid = data[1];
        var edit_attr_syntax = data[2];
        var edit_attr_multivalued = data[3];
        var edit_attr_eq_mr = data[4];
        var edit_attr_order_mr = data[5];
        var edit_attr_sub_mr = data[6];
        var edit_attr_desc = data[8];
        var edit_attr_aliases = data[9];
        if (edit_attr_eq_mr) {
          edit_attr_eq_mr = data[4]
        }
        if (edit_attr_order_mr) {
          edit_attr_order_mr = data[5]
        }
        if (edit_attr_sub_mr) {
          edit_attr_sub_mr = data[6]
        }

        $("#add-edit-attr-header").html('Edit Attribute: ' + edit_attr_name);
        $("#attr-name").val(edit_attr_name);
        $("#attr-name").attr('disabled', true);
        $("#attr-oid").val(edit_attr_oid);
        $("#attr-desc").val(edit_attr_desc);
        if (edit_attr_aliases) {
          $("#attr-alias").val(edit_attr_aliases.join(" "));
        }
        $("#attr-syntax").val(edit_attr_syntax);
        $("#attr-multivalued").val(edit_attr_syntax);
        $("#attr-multivalued").prop('checked', false);
        if (edit_attr_multivalued == "yes") {
          $("#attr-multivalued").prop('checked', true);
        }
        $("#save-attr-spinner").show();
        $("#attr-eq-mr-select")[0].value = edit_attr_eq_mr;
        $("#attr-order-mr-select")[0].value = edit_attr_order_mr;
        $("#attr-sub-mr-select")[0].value = edit_attr_sub_mr;
        $("#save-attr-spinner").hide();

        $("#add-edit-attr-form").modal('toggle');
    } );

    $(document).on('click', '.attr-del-btn', function(e) {
      e.preventDefault();
      var data = schema_at_table.row( $(this).parents('tr') ).data();
      var del_attr_name = data[0];
      var at_row = $(this);
      popup_confirm("Are you sure you want to delete attribute: <b>" + del_attr_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'attributetypes', 'remove', del_attr_name];
          console.log("CMD: remove attribute: " + cmd.join(' '));
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success("Attribute was successfully removed!")
            schema_at_table.row( at_row.parents('tr') ).remove().draw( false );
            $("#schema-list option[value='" + del_attr_name + "']").remove();
          }).fail(function(data) {
            popup_err("Attribute removal error", del_attr_name + " removal has failed: " + data.message);
          });
        }
      });
    });

    $(document).on('click', '.oc-edit-btn', function(e) {
      e.preventDefault();
      clear_oc_form();
      var data = schema_oc_table.row( $(this).parents('tr') ).data();
      var edit_oc_name = data[0];
      var edit_oc_oid = data[1];
      var edit_oc_parent = data[2]
      var edit_oc_required = data[3].split(" ");
      var edit_oc_allowed = data[4].split(" ");
        if (edit_oc_parent) {
          edit_oc_parent = data[2]
        }

      $("#save-oc-spinner").show();
      $("#add-edit-oc-header").html('Edit Objectclass: ' + edit_oc_name);
      $("#oc-name").attr('disabled', true);
      $("#oc-name").val(edit_oc_name);
      $("#oc-oid").val(edit_oc_oid);
      $("#oc-parent")[0].value = edit_oc_parent;
      $.each(edit_oc_required, function (i, item) {
        $("#oc-required-list").append($('<option>', {
          value: item,
          text : item
        }));
      });
      $.each(edit_oc_allowed, function (i, item) {
        $("#oc-allowed-list").append($('<option>', {
          value: item,
          text : item
        }));
      });
      $("#save-oc-spinner").hide();

      // Update modal html header and fields and show()
      $("#add-edit-oc-form").modal('toggle');

    });

    $(document).on('click', '.oc-del-btn', function(e) {
      e.preventDefault();
      var data = schema_oc_table.row( $(this).parents('tr') ).data();
      var del_oc_name = data[0];
      var oc_row = $(this);

      popup_confirm("Are you sure you want to delete objectclass: <b>" + del_oc_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'objectclasses', 'remove', del_oc_name];
          console.log("CMD: Remove objectclass: " + cmd.join(' '));
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success("ObjectClass was successfully removed!")
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





