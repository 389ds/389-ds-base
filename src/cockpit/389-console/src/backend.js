var prev_tree_node = null;
var ref_del_html = "<button class=\"btn btn-default del-ref-btn\" type=\"button\"><span class='glyphicon glyphicon-trash'></span> Delete</button>";
var attr_encrypt_del_html = "<button class=\"btn btn-default attr-encrypt-delete-btn\" type=\"button\">Remove Attribute</button></td>";
var index_btn_html =
  '<div class="dropdown"> ' +
     '<button class="btn btn-default dropdown-toggle" type="button" data-toggle="dropdown">' +
       'Choose Action...' +
       '<span class="caret"></span>' +
     '</button>' +
     '<ul class="dropdown-menu" role="menu">' +
       '<li><a class="db-index-save-btn">Save Index</a></li>' +
       '<li><a class="db-index-save-reindex-btn">Save & Reindex</a></li>' +
       '<li><a class="db-index-reindex-btn">Reindex Attribute</a></li>' +
       '<li><a class="db-index-delete-btn">Delete Index</a></li>' +
     '</ul>' +
   '</div>';

function customMenu (node) {
  var dblink_items = {
    "delete_link": {
      "label": "Delete DB Link",
      "icon": "glyphicon glyphicon-trash",
      "action": function (data) {
        popup_confirm("Are you sure you want to delete this Database Link?", "Confirmation", function (yes) {
          if (yes) {

           // TODO  Delete db link
          }
        });
      }
    }
  };

  var suffix_items = {
    'import': {
      "label": "Initialize Suffix",
       "icon": "glyphicon glyphicon-circle-arrow-right",
       "action": function (data) {
         var suffix_id = $(node).attr('id');
         var parent_suffix = suffix_id.substring(suffix_id.indexOf('-')+1);
         $("#root-suffix-import").val(parent_suffix);
         $("#import-ldif-file").val("");
         $("#import-ldif-form").modal('toggle');
       }
     },
     'export': {
       "label": "Export Suffix",
       "icon": "glyphicon glyphicon-circle-arrow-left",
       "action": function (data) {
         var suffix_id = $(node).attr('id');
         var parent_suffix = suffix_id.substring(suffix_id.indexOf('-')+1);
         $("#root-suffix-export").val(parent_suffix);
         $("#export-ldif-file").val("");
         $("#export-ldif-form").modal('toggle');
       }
     },
     'reindex': {
       "label": "Reindex Suffix",
       "icon": "glyphicon glyphicon-wrench",
       "action": function (data) {
         popup_confirm("This will impact DB performance during the indexing.  Are you sure you want to reindex all attributes?", "Confirmation", function (yes) {
           if (yes) {
              // TODO Reindex suffix
           }
         });
       }
     },
     "create_db_link": {
       "label": "Create Database Link",
       "icon": "glyphicon glyphicon-link",
       "action": function (data) {
         var suffix_id = $(node).attr('id');
         var parent_suffix = suffix_id.substring(suffix_id.indexOf('-')+1);
         //clear_chaining_form();   //TODO
         $("#create-db-link-form").modal('toggle');
       }
     },
     "create_sub_suffix": {
       "label": "Create Sub-Suffix",
       "icon": "glyphicon glyphicon-triangle-bottom",
       "action": function (data) {
         var suffix_id = $(node).attr('id');
         var parent_suffix = suffix_id.substring(suffix_id.indexOf('-')+1);
         $("#parent-suffix").html('<b>Parent Suffix:</b>&nbsp;&nbsp;' + parent_suffix);
         $("#add-subsuffix-dn").val(' ,' + parent_suffix);
         $("#add-subsuffix-form").modal('toggle');
       }
     },
     'delete_suffix': {
       "label": "Delete Suffix",
       "icon": "glyphicon glyphicon-remove",
       "action": function (data) {
          popup_confirm("Are you sure you want to delete suffix?", "Confirmation", function (yes) {
            if (yes) {
              // TODO
            }
          });
       }
     }
   };

   if ( $(node).attr('id').startsWith('suffix') ){
     return suffix_items;
   } else {
     // chaining
     return dblink_items;
   }
};

function get_encoded_ref () {
  var ref_encoded = $("#ref-protocol").val() + $("#ref-hostname").val();
  var ref_port = $("#ref-port").val();
  var ref_suffix = $("#ref-suffix").val();
  var ref_attrs = $("#ref-attrs").val();
  var ref_filter = $("#ref-filter").val();
  var ref_scope = $("#ref-scope").val();

  $("#preview-ref-btn").blur();

  if (ref_port != ""){
    ref_encoded += ":" + ref_port;
  }

  if (ref_suffix == "" && (ref_attrs != "" || ref_filter != "" || ref_scope != "")) {
    popup_msg("Attention!", "Missing suffix - you can not set the attributes, scope, or filter without a suffix.");
    return;
  }
  if (ref_suffix != "" || ref_attrs != "" || ref_filter != "" || ref_scope != "") {
    ref_encoded += "/" + encodeURIComponent(ref_suffix);
    if ( ref_attrs != "" ) {
      ref_encoded += "?" + encodeURIComponent(ref_attrs);
    } else if ( ref_filter != "" || ref_scope != "" ) {
      ref_encoded += "?";
    }
    if ( ref_scope != "" ) {
      ref_encoded += "?" + encodeURIComponent(ref_scope);
    } else if ( ref_filter != "" ) {
      ref_encoded += "?";
    }
    if ( ref_filter != "") {
      ref_encoded += "?" + encodeURIComponent(ref_filter);
    }
  }
  return ref_encoded;
}

function load_jstree() {
  $('#db-tree').jstree({
    "plugins": [ "contextmenu", "wholerow", "sort" ],
    "contextmenu": {
      "items" : customMenu
    },
    "core": {
      "check_callback": true
    }
  });

  $('#db-tree').jstree('select_node', 'ul > li:first');


  $('#db-tree').on("changed.jstree", function (e, data) {
    var node_type = data.selected[0];
    var suffix = data.instance.get_node(data.selected[0]).text.replace(/(\r\n|\n|\r)/gm,"");

    if (node_type.startsWith("dblink")) {
      var parent_suffix = node_type.substring(node_type.indexOf('-')+1);
      $(".all-pages").hide();
      $("#database-content").show();
      $("#db-page").show();
      $("#chaining-header").html("Database Chaining Configuration <font size=\"2\">(<b>" + parent_suffix + "</b>)</font>");
      $("#chaining-page").show();
    } else {
      // suffix
      $(".all-pages").hide();
      $("#database-content").show();
      $("#db-page").show();
      $("#suffix-header").html("Suffix Configuration <font size=\"2\">(<b>" + suffix + "</b>)</font>");
      $("#suffix-page").show();
    }
  });
};

function clear_ref_form () {
  $("#ref-scope").prop('selectedIndex',0);
  $("#ref-protocol").prop('selectedIndex',0);
  $("#ref-suffix").val("");
  $("#ref-filter").val("");
  $("#ref-hostname").val("");
  $("#ref-port").val("");
  $("#ref-attrs").val("");
  $("#ref-preview-field").val("");
}

function clear_index_form () {
  $("#index-list-select").prop('selectedIndex',0);
  $("#add-index-type-eq").prop('checked', false);
  $("#add-index-type-pres").prop('checked', false);
  $("#add-index-type-sub").prop('checked', false);
  $("#add-index-type-approx").prop('checked', false);
  $("#add-index-matchingrules").val("");
}

function clear_attr_encrypt_form() {
  $("#attr-encrypt-list").prop('selectedIndex',0);
  $("#nsencryptionalgorithm").prop('selectedIndex',0);
}

$(document).ready( function() {
  $("#database-content").load("backend.html", function () {
    load_jstree();

    $(".ds-suffix-panel").toggle("active");
    $(".ds-suffix-panel").css('display','none');

    $("#create-ref-btn").on("click", function () {
      clear_ref_form();
    });

    $("#db-chaining-btn").on("click", function() {
      $(".all-pages").hide();
      $("#database-content").show();
      $("#db-chaining-settings-page").show();
    });
    $("#db-global-btn").on("click", function() {
      $(".all-pages").hide();
      $("#database-content").show();
      $("#db-global-page").show();
    });
    $("#db-suffix-btn").on("click", function() {
      $(".all-pages").hide();
      $("#database-content").show();
      $("#db-page").show();
      $("#suffix-page").show();
    });

    $("#chaining-adv-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Advanced Database Link Settings ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Advanced Database Link Settings";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });


    $("#db-system-index-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show System Indexes ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide System Indexes ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    $("#db-index-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Database Indexes ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Database Indexes ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    $("#suffix-attrencrypt-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Encrypted Attributes ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Encrypted Attributes ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });


    /*
      We need logic to see if autocaching (import and db) is being used, and disable fields,
      and set radio buttons, et
    */

    var ref_table = $('#referral-table').DataTable( {
      "paging": false,
      "searching": false,
      "bInfo" : false,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No Referrals"
      },
      "columnDefs": [
        {
          "targets": 1,
          "orderable": false,
        },
        {
           "targets": 1,
           "className": "ds-center"
        }
      ]
    });


    $('#system-index-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "search": "Search",
        "emptyTable": "No System Indexes"
      },
      "columns": [
        { "width": "10%" },
        { "width": "20px" },
        { "width": "20px" },
        { "width": "20px" },
        { "width": "20px" },
        { "width": "20%" }
      ],
    });
    var index_table = $('#index-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "search": "Search",
        "emptyTable": "No Indexes"
      },
      "columns": [
        { "width": "10%" },
        { "width": "15px" },
        { "width": "15px" },
        { "width": "15px" },
        { "width": "15px" },
        { "width": "20%" },
        { "width": "76px" }
      ],
      "columnDefs": [ {
        "targets": 6,
        "orderable": false
      } ]
    });




    var attr_encrypt_table = $('#attr-encrypt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "searching": true,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "search": "Search",
        "emptyTable": "No Encrypted Attributes"
      },
      "columnDefs": [ {
        "targets": 2,
        "orderable": false
      } ]
    });

    // Accordion opening/closings
    $(".ds-accordion-panel").css('display','none');

    var suffix_acc = document.getElementsByClassName("suffix-accordion");
    for (var i = 0; i < suffix_acc.length; i++) {
      suffix_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    $("#db-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Advanced Settings";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Advanced Settings ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    var db_acc = document.getElementsByClassName("db-accordion");
    for (var i = 0; i < db_acc.length; i++) {
      db_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var cache_acc = document.getElementsByClassName("cache-accordion");
    for (var i = 0; i < cache_acc.length; i++) {
      cache_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }


    $(".index-type").attr('readonly', true);

    if ( $("#manual-cache").is(":checked") ){
      $("#auto-cache-form").hide();
      $("#manual-cache-form").show();
      $("#nsslapd-dncachememsize").prop('disabled', false);
      $("#nsslapd-dncachememsize").val('');
      $("#nsslapd-cachememsize").prop('disabled', false);
      $("#nsslapd-cachememsize").val('');
      $("#nsslapd-cachesize").prop('disabled', false);
      $("#nsslapd-cachesize").val('');
    } else {
      $("#manual-cache-form").hide();
      $("#auto-cache-form").show();
      $("#nsslapd-dncachememsize").prop('disabled', true);
      $("#nsslapd-dncachememsize").val('AUTOTUNED');
      $("#nsslapd-cachememsize").prop('disabled', true);
      $("#nsslapd-cachememsize").val('AUTOTUNED');
      $("#nsslapd-cachesize").prop('disabled', true);
      $("#nsslapd-cachesize").val('AUTOTUNED');
    }

    if ( $("#manual-import-cache").is(":checked") ){
      $("#auto-import-cache-form").hide();
      $("#manual-import-cache-form").show();
    } else {
      $("#manual-import-cache-form").hide();
      $("#auto-import-cache-form").show();
    }

    $(".cache-role").on("change", function() {
      var cache_role = $("input[name=cache-role]:checked").val();
      if (cache_role == "manual-cache") {
        $("#auto-cache-form").hide();
        $("#manual-cache-form").show();
      } else {
        // auto cache
        $("#manual-cache-form").hide();
        $("#auto-cache-form").show();
      }
    });

    $(document).on('click', '.del-ref-btn', function(e) {
      e.preventDefault();
      var data = ref_table.row( $(this).parents('tr') ).data();
      var del_ref_name = data[0];
      var ref_row = $(this); // Store element for callback
      popup_confirm("Are you sure you want to delete referral:  <b>" + del_ref_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          // TODO Delete mapping from DS

          // Update html table
          ref_table.row( ref_row.parents('tr') ).remove().draw( false );
        }
      });
    });

    $("#backend-config-save-btn").on("click", function () {
      var role = $("input[name=cache-role]:checked").val();
      if (role == "manual-cache") {
        // TODO - need to get cache values and fields (overwrite AUTOTUNED)
        $("#nsslapd-dncachememsize").prop('disabled', false);
        $("#nsslapd-dncachememsize").val('')
        $("#nsslapd-cachememsize").prop('disabled', false);
        $("#nsslapd-cachememsize").val('');
        $("#nsslapd-cachesize").prop('disabled', false);
        $("#nsslapd-cachesize").val('');
      } else {
        // auto cache
        $("#nsslapd-dncachememsize").prop('disabled', true);
        $("#nsslapd-dncachememsize").val('AUTOTUNED');
        $("#nsslapd-cachememsize").prop('disabled', true);
        $("#nsslapd-cachememsize").val('AUTOTUNED');
        $("#nsslapd-cachesize").prop('disabled', true);
        $("#nsslapd-cachesize").val('AUTOTUNED');
      }
    });

    $(".import-cache-role").on("change", function() {
      var role = $("input[name=import-cache-role]:checked").val();
      if (role == "manual-import-cache") {
         $("#auto-import-cache-form").hide();
         $("#manual-import-cache-form").show();
      } else {
          // auto cache
         $("#manual-import-cache-form").hide();
         $("#auto-import-cache-form").show();
      }
    });

    // Based on the db-link connection type change the agmt-auth options
    $("#dblink-conn").change(function() {
      var ldap_opts = {"Simple": "Simple",
                       "SASL/DIGEST-MD5": "SASL/DIGEST-MD5",
                       "SASL/GSSAPI": "SASL/GSSAPI"};
      var ldaps_opts = {"Simple": "Simple",
                        "SSL Client Authentication": "SSL Client Authentication",
                        "SASL/DIGEST-MD5": "SASL/DIGEST-MD5"};
      var $auth = $("#nsbindmechanism");
      $auth.empty();
      var conn = $('#dblink-conn').val();
      if (conn == "LDAP"){
        $.each(ldap_opts, function(key, value) {
          $auth.append($("<option></option>").attr("value", value).text(key));
        });
      } else {
        // TLS options
        $.each(ldaps_opts, function(key, value) {
          $auth.append($("<option></option>").attr("value", value).text(key));
        });
      }
      $("#nsmultiplexorbinddn").prop('disabled', false);
      $("#nsmultiplexorcredentials").prop('disabled', false);
      $("#nsmultiplexorcredentials-confirm").prop('disabled', false);
    });

    // Check for auth changes and disable/enable bind DN & password for db-links
    $("#nsbindmechanism").change(function() {
      var authtype = $('#nsbindmechanism').val();
      if (authtype == "SSL Client Authentication") {
        $("#nsmultiplexorbinddn").prop('disabled', true);
        $("#nsmultiplexorcredentials").prop('disabled', true);
        $("#nsmultiplexorcredentials-confirm").prop('disabled', true);
      } else {
        $("#nsmultiplexorbinddn").prop('disabled', false);
        $("#nsmultiplexorcredentials").prop('disabled', false);
        $("#nsmultiplexorcredentials-confirm").prop('disabled', false);
      }
    });

    //
    // Modal Forms
    //

    // Chaining OIDS
    $("#chaining-oid-button").on("click", function() {
      $(this).blur();
    });
    $("#chaining-oid-save").on("click", function() {
      // Update oids
      var chaining_oids = $("#avail-chaining-oid-list").val();
      for (var i = 0; chaining_oids && i < chaining_oids.length; i++) {
        $('#chaining-oid-list').append($('<option/>', {
          value: chaining_oids[i],
          text : chaining_oids[i]
        }));
        $("#avail-chaining-oid-list option[value='" + chaining_oids[i] + "']").remove();
      }
      sort_list( $("#chaining-oid-list") );
      $("#chaining-oid-form").modal('toggle');
    });
    $("#delete-chaining-oid-button").on("click", function() {
      var oids = $("#chaining-oid-list").find('option:selected');
      if (oids && oids != '' && oids.length > 0) {
        for (var i = 0; i < oids.length; i++) {
          if ( $('#avail-chaining-comp-list option[value="' + oids[i].text + '"]').val() === undefined) {
            $('#avail-chaining-oid-list').append($("<option/>").val(oids[i].text).text(oids[i].text));
          }
        }
      }
      $("#chaining-oid-list").find('option:selected').remove();
      sort_list( $('#avail-chaining-oid-list') );
    });

    // Chaining Comps
    $("#delete-chaining-comp-button").on("click", function() {
      var comps = $("#chaining-comp-list").find('option:selected');
      if (comps && comps != '' && comps.length > 0) {
        for (var i = 0; i < comps.length; i++) {
          if ( $('#avail-chaining-comp-list option[value="' + comps[i].text + '"]').val() === undefined) {
            $('#avail-chaining-comp-list').append($("<option/>").val(comps[i].text).text(comps[i].text));
          }
        }
      }
      $("#chaining-comp-list").find('option:selected').remove();
      sort_list($('#avail-chaining-comp-list') );
    });
    $("#chaining-comp-save").on("click", function() {
      // Update comps
      var chaining_comps = $("#avail-chaining-comp-list").val();
      for (var i = 0; chaining_comps && i < chaining_comps.length; i++) {
        $('#chaining-comp-list').append($('<option/>', {
          value: chaining_comps[i],
          text : chaining_comps[i]
        }));
        $("#avail-chaining-comp-list option[value='" + chaining_comps[i] + "']").remove();
      }
      sort_list( $("#chaining-comp-list") );
      $("#chaining-comp-form").css('display', 'none');
    });

    // Create DB Link
    $("#create-chain-close").on("click", function() {
      $("#create-db-link-form").css('display', 'none');
    });
    $("#chaining-cancel").on("click", function() {
      $("#create-db-link-form").css('display', 'none');
    });
    $("#chaining-save").on("click", function() {
      // Create DB link, if LDAPS is selected replace remotefarmUrl "ldap://" with "ldaps://", and visa versa to remove ldaps://
      var chaining_name = $("#chaining-name").val();
      var parent_suffix = $("#db-tree").jstree().get_selected(true)[0];
      var suffix = $("#db-tree").jstree().get_selected(true)[0].text.replace(/(\r\n|\n|\r)/gm,"");

      // TODO - create db link in DS

      $('#db-tree').jstree().create_node(parent_suffix,
                                         { "id" : "dblink-" + suffix, "text" : chaining_name, "icon" : "glyphicon glyphicon-link" },
                                         "last");
      $("#create-db-link-form").css('display', 'none');
    });

    // Add Index
    $("#add-index-button").on("click", function() {
      clear_index_form();
    })

    $("#add-index-save").on("click", function() {
      var attr_name = $("#index-list-select").val();
      var add_attr_type_eq = '<input type="checkbox" id="' + attr_name + '-eq">';
      var add_attr_type_pres = '<input type="checkbox" id="' + attr_name + '-pres">';
      var add_attr_type_sub = '<input type="checkbox" id="' + attr_name + '-sub">';
      var add_attr_type_approx = '<input type="checkbox" id="' + attr_name + '-approx">';
      var add_index_matchingrules = $("#add-index-matchingrules").val();

      if ( $("#add-index-type-eq").is(":checked") ){
        add_attr_type_eq = '<input type="checkbox" id="' + attr_name + '-eq" checked>';
      }
      if ( $("#add-index-type-pres").is(":checked") ){
        add_attr_type_pres = '<input type="checkbox" id="' + attr_name + '-pres" checked>';
      }
      if ( $("#add-index-type-sub").is(":checked") ){
        add_attr_type_sub = '<input type="checkbox" id="' + attr_name + '-sub" checked>';
      }
      if ( $("#add-index-type-approx").is(":checked") ){
        add_attr_type_approx = '<input type="checkbox" id="' + attr_name + '-approx" checked>';
      }

      // TODO - add index to DS

      // Update table on success
      index_table.row.add( [
        attr_name,
        add_attr_type_eq,
        add_attr_type_pres,
        add_attr_type_sub,
        add_attr_type_approx,
        add_index_matchingrules,
        index_btn_html
      ] ).draw( false );

      $("#add-index-form").css('display', 'none');
      clear_index_form();
      // Do the actual save in DS
      // Update html
    });


    // Add encrypted attribute
    $("#add-encrypted-attr-button").on("click", function() {
      clear_attr_encrypt_form();
    })
    $("#add-encrypted-attr-save").on("click", function() {

      // Do the actual save in DS
      // Update html

      var encrypt_attr_name = $("#attr-encrypt-list").val();
      var attr_encrypt_algo = $("#nsencryptionalgorithm").val();
      // TODO - add encrypted attr to DS

      // Update table on success
      attr_encrypt_table.row.add( [
        encrypt_attr_name,
        attr_encrypt_algo,
        attr_encrypt_del_html
      ] ).draw( false );

      $("#add-encrypted-attr-form").modal('toggle');

    });

    // Create Suffix
    $("#add-suffix-save").on("click", function() {
      var suffix = $("#add-suffix-dn").val();
      var backend = $("#add-suffix-backend").val();

      // TODO - create suffix in DS

      $('#db-tree').jstree().create_node('db-root',
                                         { "id" : "suffix-" + suffix, "text" : suffix, "icon" : "glyphicon glyphicon-tree-conifer" },
                                         "last");
      $("#add-suffix-form").css('display', 'none');
    });

    // Create Sub Suffix
    $("#add-subsuffix-close").on("click", function() {
      $("#add-subsuffix-form").css('display', 'none');
    });
    $("#add-subsuffix-cancel").on("click", function() {
      $("#add-subsuffix-form").css('display', 'none');
    });
    $("#add-subsuffix-save").on("click", function() {
      var suffix = $("#add-subsuffix-dn").val();
      var backend = $("#add-subsuffix-backend").val();
      var parent_suffix = $("#db-tree").jstree().get_selected(true)[0];

      // TODO - create suffix in DS

      $('#db-tree').jstree().create_node(parent_suffix,
                                         { "id" : "subsuffix-" + suffix, "text" : suffix, "icon" : "glyphicon glyphicon-leaf" },
                                         "last");
      $("#add-subsuffix-form").css('display', 'none');
    });

    // Init Suffix (import)
    $("#import-ldif-save").on("click", function() {
      var root_suffix_import = $("#root-suffix-import").val();
      var ldif_file_import = $("#ldif-file-import").val();
      var exclude_suffix_import = $("#exclude-suffix-import").val();
      var include_suffix_import = $("#include-suffix-import").val();
      var cmd = [DSCONF, server_inst, 'backend', 'import', root_suffix_import];
      // Process and validate parameters
      if (ldif_file_import == ""){
        popup_msg("Error", "LDIF file should be specified");
        return;
      } else if (ldif_file_import.indexOf(' ') >= 0) {
        popup_msg("Error", "LDIF file can not contain any spaces");
        return;
      } else if (ldif_file_import.indexOf('/') === -1 ) {
        popup_msg("Error", "LDIF file can not contain a forward slash. " +
                           "LDIF files are written to the server's LDIF directory (nsslapd-ldifdir)");
        return;
      } else {
        cmd.push(ldif_file_import);
      }
      if (include_suffix_import != "") {
        cmd.push.apply(cmd, ["-s", include_suffix_import]);
      }
      if (exclude_suffix_import != "") {
        cmd.push.apply(cmd, ["-x", exclude_suffix_import]);
      }
      $("#import-ldif-spinner").show();
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#import-ldif-spinner").hide();
        popup_success("LDIF has been imported");
        $("#import-ldif-form").modal('toggle');
      }).
      fail(function(data) {
        $("#import-ldif-spinner").hide();
        popup_err("Error", "Failed to import LDIF\n" + data.message);
      })
    });

    // Export Suffix (import)
    $("#export-ldif-close").on("click", function() {
      $("#export-ldif-form").css('display', 'none');
    });
    $("#export-ldif-cancel").on("click", function() {
      $("#export-ldif-form").css('display', 'none');
    });
    $("#export-ldif-save").on("click", function() {
      var root_suffix_export = $("#root-suffix-export").val();
      var ldif_file_export = $("#ldif-file-export").val();
      var exclude_suffix_export = $("#exclude-suffix-export").val();
      var include_suffix_export = $("#include-suffix-export").val();
      var cmd = [DSCONF, server_inst, 'backend', 'export', root_suffix_export];
      // Process and validate parameters
      if (ldif_file_export.indexOf(' ') >= 0) {
        popup_msg("Error", "LDIF file can not contain any spaces");
        return;
      } else if (ldif_file_import.indexOf('/') === -1 ) {
        popup_msg("Error", "LDIF file can not contain a forward slash. " +
                           "LDIF files are written to the server's LDIF directory (nsslapd-ldifdir)");
        return;
      } else if (ldif_file_export != ""){
        cmd.push.apply(cmd, ["-l", ldif_file_export]);
      }
      if (include_suffix_export != "") {
        cmd.push.apply(cmd, ["-s", include_suffix_export]);
      }
      if (exclude_suffix_export != "") {
        cmd.push.apply(cmd, ["-x", exclude_suffix_export]);
      }
      $("#export-ldif-spinner").show();
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
      done(function(data) {
        $("#export-ldif-spinner").hide();
        popup_success("LDIF has been exported");
        $("#export-ldif-form").modal('toggle');
      }).
      fail(function(data) {
        $("#export-ldif-spinner").hide();
        popup_err("Error", "Failed to export LDIF\n" + data.message);
      })
    });

    $("#create-ref-save").on("click", function() {
      var ref = get_encoded_ref();
      // Do the actual save in DS
      // Update html
      var tr_row = ref_table.row.add( [
        ref,
        ref_del_html
      ] );
      $( tr_row ).addClass('ds-nowrap-td');
      $( tr_row ).find("td:nth-child(1))").addClass('ds-center');
      tr_row.draw(false);
      $("#create-ref-form").modal('toggle');
    });

    $("#preview-ref-btn").on('click', function() {
      $("#ref-preview-field").val(get_encoded_ref());
    });

    $(document).on('click', '.delete-referral-btn', function(e) {
      e.preventDefault();
      var data = ref_table.row( $(this).parents('tr') ).data();
      var del_ref_name = data[0];
      var ref_row = $(this);
      popup_confirm("Are you sure you want to delete referral: " + del_ref_name, "Confirmation", function (yes) {
        if (yes) {
          // TODO Delete ref
          ref_table.row( ref_row.parents('tr') ).remove().draw( false );
        }
      });
    });


    // suffix index actions
    $(document).on('click', '.db-index-save-btn', function(e) {
      e.preventDefault();
      var data = index_table.row( $(this).parents('tr') ).data();
      var save_index_name = data[0];
        // Do save!
    });

    $(document).on('click', '.db-index-reindex-btn', function(e) {
      e.preventDefault();
      var data = index_table.row( $(this).parents('tr') ).data();
      var reindex_name = data[0];
      popup_confirm("Are you sure you want to reindex attribute: <b>" + reindex_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          // TODO reindex attr

        }
      });
    });

    $(document).on('click', '.db-index-delete-btn', function(e) {
      e.preventDefault();
      var data = index_table.row( $(this).parents('tr') ).data();
      var del_index_name = data[0];
      var index_row = $(this);

      popup_confirm("Are you sure you want to delete index: <b>" + del_index_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          // TODO Delete index
          index_table.row( index_row.parents('tr') ).remove().draw( false );
        }
      });
    });

    // Attribute encryption
    $(document).on('click', '.attr-encrypt-delete-btn', function(e) {
      e.preventDefault();
      var data = attr_encrypt_table.row( $(this).parents('tr') ).data();
      var attr_name = data[0];
      var eattr_row = $(this);
      popup_confirm("Are you sure you want to delete encrypted attribute: <b>" + attr_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          // TODO Delete ref
          attr_encrypt_table.row( eattr_row.parents('tr') ).remove().draw( false );
        }
      });
    });

    // Page is loaded, mark it as so...
    db_page_loaded = 1;
  });
});

