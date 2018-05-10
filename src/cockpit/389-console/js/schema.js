var attr_btn_html = 
  '<div class="dropdown">' +
    '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="" data-toggle="dropdown">' +
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
    '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="oc-action-btn" data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="oc-action-btn">' +
      '<li role=""><a role="menuitem" tabindex="0" class="oc-edit-btn" href="#">Edit Objectclass</a></li>' +
      '<li role=""><a role="menuitem" tabindex="1" class="oc-del-btn" href="#">Delete Objectclass</a></li>' +
    '</ul>' +
  '</div>';

function clear_oc_form() {
  // Clear input fields and reset dropboxes
  $("#add-edit-oc-header").html('Add Objectclass');
  $("#oc-name").attr('disabled', false);
  $("#oc-name").val("");
  $("#oc-oid").val("");
  $("#oc-parent").prop('selectedIndex',0);
  $("#schema-list").prop('selectedIndex',-1);
  $('#oc-required-list').find('option').remove();
  $('#oc-allowed-list').find('option').remove();
};

function clear_attr_form() {
  // Clear input fields and reset dropboxes
  $(".ds-accordion-panel").css('display','none');
  $("#add-edit-attr-header").html('Add Attribute');
  $("#attr-name").attr('disabled', false);
  $("#attr-name").val("");
  $("#attr-syntax").val("");
  $("#attr-desc").val("");
  $("#attr-oid").val("");
  $("#attr-alias").val("");
  $('#attr-multivalued').prop('checked', false);
  $("#attr-eq-mr-select").prop('selectedIndex',0);
  $("#attr-order-mr-select").prop('selectedIndex',0);
  $("#attr-sub-mr-select").prop('selectedIndex',0);
};


$(document).ready( function() {
  $("#schema-content").load("schema.html", function (){

    // TODO Get attributes, Objectclasses, syntaxes, and matching rules: populate tables and forms

    // Setup the tables: standard, custom, and Matching Rules
    $('#standard-oc-table').DataTable({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
      "language": {
        "emptyTable": "No objectclasses available",
        "search": "Search Objectclasses"
      },
    });

    $('#standard-attr-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
      "language": {
        "emptyTable": "No attributes available",
        "search": "Search Attributes"
      },
    });


    var custom_oc_table = $('#custom-oc-table').DataTable ({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
      "language": {
        "emptyTable": "No custom objectclasses defined",
        "search": "Search Objectclasses"
      },
      "columnDefs": [ {
        "targets": 5,
        "orderable": false
      } ]
    });
    var custom_at_table = $('#custom-attr-table').DataTable({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
      "language": {
        "emptyTable": "No custom attributes defined",
        "search": "Search Attributes"
      },
      "columnDefs": [ {
        "targets": 7,
        "orderable": false
      } ]
    });

    $('#schema-mr-table').DataTable({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No matching rules defined",
        "search": "Search Matching Rules"
      },
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
    });

    // Load everything from DS and update html

    // Sort schema list awhile
    sort_list( $("#schema-list") );

    $("#std-schema-btn").on("click", function() {
      $(".all-pages").hide();
      $("#schema-content").show();
      $("#std-schema-page").show();
    });
    $("#custom-schema-btn").on("click", function() {
      $(".all-pages").hide();
      $("#schema-content").show();
      $("#custom-schema-page").show();
    });
    $("#schema-mr-btn").on("click", function() {
      $(".all-pages").hide();
      $("#schema-content").show();
      $("#schema-mr").show();
    });

    // Accordions
    $(".ds-accordion-panel").css('display','none');
    var attr_opts_acc = document.getElementsByClassName("attr-opts-accordion");
    for (var i = 0; i < attr_opts_acc.length; i++) {
      attr_opts_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display == "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

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
      $("#add-edit-oc-form").css('display', 'block');
    });

    $("#add-edit-oc-close").on("click", function() {
      $("#add-edit-oc-form").css('display', 'none');
    });
    $("#add-edit-oc-cancel").on("click", function() {
      $("#add-edit-oc-form").css('display', 'none');
    });
    $("#add-edit-oc-save").on("click", function() {
      
      // Do the actual save in DS



      // Update html
      // If save successful close down form, otherwise keep form up and return      
      $("#add-edit-oc-form").css('display', 'none');
      $("#oc-name").attr('disabled', false);
  
  
      // Convert allowed/requires list to html format
      var oc_required_list = $('#oc-required-list option').map(function() { return $(this).val(); }).get().join(', ');
      var oc_allowed_list = $('#oc-allowed-list option').map(function() { return $(this).val(); }).get().join(', ');

      // Update html table
      var oc_name = $("#oc-name").val();
      custom_oc_table.row.add( [
        oc_name,
        $("#oc-oid").val(),
        $("#oc-parent").val(),
        tableize(oc_required_list),
        tableize(oc_allowed_list),
        oc_btn_html
      ] ).draw( false );
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
      $("#add-edit-attr-form").css('display', 'block');
    })
    $("#attr-close").on("click", function() {
      $("#add-edit-attr-form").css('display', 'none');
    });
    $("#attr-cancel").on("click", function() {
      $("#add-edit-attr-form").css('display', 'none');
    });
    $("#attr-save").on("click", function() {
      var attr_name = $("#attr-name").val();
      var multiple = "no";
      var eq_mr= $('#attr-eq-mr-select').val();
      var order_mr = $('#attr-order-mr-select').val();
      var sub_mr  = $('#attr-sub-mr-select').val();
      var multiple = "no";
      if ( $("#attr-multivalued").is(":checked") ) {
        multiple = "yes";
      };

      // Do the actual save in DS
      
      // if save in DS successful close down form, otherwise keep form visible and return ^^^
      $("#add-edit-attr-form").css('display', 'none');
      $("#attr-name").attr('disabled', false);

      // Create attribute row to dataTable with correct edit button id
      custom_at_table.row.add( [
            attr_name,
            $("#attr-oid").val(),
            $("#attr-syntax").val(),
            multiple,
            eq_mr,
            order_mr,
            sub_mr,
            attr_btn_html
        ] ).draw( false );
    });

    $(document).on('click', '.attr-edit-btn', function(e) {
        e.preventDefault();
        clear_attr_form();
        var data = custom_at_table.row( $(this).parents('tr') ).data();
        var edit_attr_name = data[0];
        var edit_attr_oid = "";
        var edit_attr_desc = "";
        var edit_attr_alias = "";
        var edit_attr_syntax = "";
        var edit_attr_multivalued = "";
        var edit_attr_eq_mr = "";
        var edit_attr_order_mr = "";
        var edit_attr_sub_mr = "";
        
        console.log("In edit attr,,,");
        $("#add-edit-attr-header").html('Edit Attribute: ' + edit_attr_name);
        $("#attr-name").val(edit_attr_name);
        $("#attr-name").attr('disabled', true);

        $("#add-edit-attr-form").css('display', 'block');
        
        // TODO Get fresh copy of attr to fill in edit form

        // Update modal html header and fields and show()
    } );

    $(document).on('click', '.attr-del-btn', function(e) {
      e.preventDefault();
      var data = custom_at_table.row( $(this).parents('tr') ).data();
      var del_attr_name = data[0];
      var at_row = $(this);
      bootpopup.confirm("Are you sure you want to delete attribute: " + del_attr_name, "Confirmation", function (yes) {
        if (yes) {
          // TODO Delete attr from DS

          // Update html table
          custom_at_table.row( at_row.parents('tr') ).remove().draw( false );
        }
      });
    });

    $(document).on('click', '.oc-edit-btn', function(e) {
      e.preventDefault();
      clear_oc_form();
      var data = custom_oc_table.row( $(this).parents('tr') ).data();
      var edit_oc_name = data[0];
      var edit_oc_oid = "";
      var edit_oc_desc = "";
      var edit_oc_parent = "";
      var edit_oc_required = "";
      var edit_oc_allowed = "";

      $("#oc-name").attr('disabled', true);
      $("#oc-name").val(edit_oc_name);
      $("#add-edit-oc-header").html('Edit Objectclass: ' + edit_oc_name);
      
      // TODO Get fresh copy of objectclass for edit form

      // Update modal html header and fields and show()
      $("#add-edit-oc-form").css('display', 'block');
      
    } );

    $(document).on('click', '.oc-del-btn', function(e) {
      e.preventDefault();
      var data = custom_oc_table.row( $(this).parents('tr') ).data();
      var del_oc_name = data[0];
      var oc_row = $(this);

      bootpopup.confirm("Are you sure you want to delete objectclass: " + del_oc_name, "Confirmation", function (yes) {
        if (yes) {
          // TODO Delete attr from DS

          // Update html table
          custom_oc_table.row( oc_row.parents('tr') ).remove().draw( false );
        }
      });

    });
  });
});





