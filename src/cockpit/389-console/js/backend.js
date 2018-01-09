

function customMenu (node) {
  var root_items = {
    "create_suffix": {
      "label": "Create Suffix",
      "icon": "glyphicon glyphicon-plus",
      "action": function (data) {
        // TODO Create suffix
      }
    }
  };

  var dblink_items = {
    "delete_link": {
      "label": "Delete DB Link",
      "icon": "glyphicon glyphicon-trash",
      "action": function (data) {
        // TODO Delete db link
       }
     }
  };

  var suffix_items = {
     'import': {
          "label": "Initialize Suffix",
          "icon": "glyphicon glyphicon-circle-arrow-right",
         "action": function (data) {
           // TODO Import suffix
          }
      },
      'export': {
          "label": "Export Suffix",
          "icon": "glyphicon glyphicon-circle-arrow-left",
         "action": function (data) {
           // TODO Export suffix
          }
      },
      'reindex': {
          "label": "Reindex Suffix",
          "icon": "glyphicon glyphicon-wrench",
         "action": function (data) {
           // TODO Reindex suffix
          }
      },
      "create_db_link": {
        "label": "Create Database Link",
        "icon": "glyphicon glyphicon-link",
        "action": function (data) {
          var suffix_id = $(node).attr('id');
          var parent_suffix = suffix_id.substring(suffix_id.indexOf('-')+1);
          //clear_chaining_form();   //TODO
          $("#create-db-link-form").css('display', 'block');
         }
      },
      "create_sub_suffix": {
        "label": "Create Sub-Suffix",
        "icon": "glyphicon glyphicon-triangle-bottom",
        "action": function (data) {
          // TODO reate suffix

         }
      },
      'delete_suffix': {
          "label": "Delete Suffix",
          "icon": "glyphicon glyphicon-remove",
         "action": function (data) {
           // TODO Delete suffix
          }
      }
   };

   if ( $(node).attr('id') == "root" ) {
     return root_items;
   } else if ( $(node).attr('id').startsWith('suffix') ){
     return suffix_items;
   } else {
     // chaining
     return dblink_items;
   }
};

function load_jstree() {
  $('#tree').jstree({
    "plugins": [ "contextmenu", "wholerow" ],
    "contextmenu": {
      "items" : customMenu
   }
  });

  $('#tree').on("changed.jstree", function (e, data) {
    var node_type = data.selected[0];
    var suffix = data.instance.get_node(data.selected[0]).text.replace(/(\r\n|\n|\r)/gm,"");

    console.log("The selected nodes are: " + node_type + " = " + suffix);

    if (node_type == "root"){
      $("#suffix").hide();
      $("#chaining").hide();
      $("#db").show();
    } else if (node_type.startsWith("dblink")) {
      var parent_suffix = node_type.substring(node_type.indexOf('-')+1);
      $("#db").hide();
      $("#suffix").hide();
      $("#chaining-header").html("Database Chaining Configuration <font size=\"2\">(<b>" + parent_suffix + "</b>)</font>");
      $("#chaining").show();
    } else {
      // suffix
      $("#db").hide();
      $("#chaining").hide();
      $("#suffix-header").html("Suffix Configuration <font size=\"2\">(<b>" + suffix + "</b>)</font>");
      $("#suffix").show();
    }
  });
};

$(document).ready( function() {
  $("#backend-selection").load("backend.html", function () {
    load_jstree();
    $("#db").show();

    $(".ds-suffix-panel").toggle("active");
    $(".ds-suffix-panel").css('display','none');

    /*
      We need logic to see if autocaching (import and db) is being used, and disable fields, 
      and set radio buttons, et
    */

    $('#referral-table').DataTable( {
      "paging": false,
      "searching": false,
      "bInfo" : false,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No Referrals"
      },
      "columnDefs": [ {
        "targets": 1,
        "orderable": false
      } ]
    });

    $('#system-index-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No System Indexes"
      },
    });
    $('#index-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No Indexes"
      },
      "columnDefs": [ {
        "targets": 6,
        "orderable": false
      } ]
    });
    $('#attr-encrypt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
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

    var chain_adv_acc = document.getElementsByClassName("chaining-adv-accordion");
    for (var i = 0; i < chain_adv_acc.length; i++) {
      chain_adv_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }


    $(".index-type").attr('readonly', 'readonly');

    if ( $("#manual-cache").is(":checked") ){
      $("#auto-cache-form").hide();
      $("#manual-cache-form").show();
      $("#nsslapd-dncachememsize").prop('disabled', false);
      $("#nsslapd-cachememsize").prop('disabled', false);
      $("#nsslapd-cachesize").prop('disabled', false);
    } else {
      $("#manual-cache-form").hide();
      $("#auto-cache-form").show();
      $("#nsslapd-dncachememsize").prop('disabled', true);
      $("#nsslapd-cachememsize").prop('disabled', true);
      $("#nsslapd-cachesize").prop('disabled', true);
    }

    if ( $("#manual-import-cache").is(":checked") ){
      $("#auto-import-cache-form").hide();
      $("#manual-import-cache-form").show();
    } else {
      $("#manual-import-cache-form").hide();
      $("#auto-import-cache-form").show();
    }

    $(".cache-role").on("change", function() {
      var role = $("input[name=cache-role]:checked").val();
      if (role == "manual-cache") {
         $("#auto-cache-form").hide();
         $("#manual-cache-form").show();
         $("#nsslapd-dncachememsize").prop('disabled', false);
         $("#nsslapd-cachememsize").prop('disabled', false);
         $("#nsslapd-cachesize").prop('disabled', false);
      } else {
          // auto cache
         $("#manual-cache-form").hide();
         $("#auto-cache-form").show();
         $("#nsslapd-dncachememsize").prop('disabled', true);
         $("#nsslapd-cachememsize").prop('disabled', true);
         $("#nsslapd-cachesize").prop('disabled', true);
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
    $("#chain-oid-close").on("click", function() {
      $("#chaining-oids-form").css('display', 'none');
    });
    $("#chaining-oid-cancel").on("click", function() {
      $("#chaining-oids-form").css('display', 'none');
    });
    $("#chaining-oid-button").on("click", function() {
      // Update oids
      $("#chaining-oids-form").css('display', 'block');
    })
    $("#chaining-oid-save").on("click", function() {
      // Update oids
      $("#chaining-oids-form").css('display', 'none');
    });

    // Chaining Comps
    $("#chain-comp-close").on("click", function() {
      $("#chaining-comp-form").css('display', 'none');
    });
    $("#chaining-comp-cancel").on("click", function() {
      $("#chaining-comp-form").css('display', 'none');
    });
    $("#chaining-comp-button").on("click", function() {
      // Update Comps
      $("#chaining-comp-form").css('display', 'block');
    })
    $("#chaining-comp-save").on("click", function() {
      // Update comps
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
      $("#create-db-link-form").css('display', 'none');
    });

  });
});



/*

Global Settings
=====================================
nsslapd-lookthroughlimit: 5000
nsslapd-idlistscanlimit: 4000
nsslapd-dbcachesize: 536870912
nsslapd-import-cache-autosize: -1
nsslapd-import-cachesize: 16777216
nsslapd-cache-autosize: 10
nsslapd-cache-autosize-split: 40
nsslapd-import-cachesize: 16777216
nsslapd-pagedlookthroughlimit: 0
nsslapd-pagedidlistscanlimit: 0
nsslapd-rangelookthroughlimit: 5000




Advanced:
=====================
nsslapd-mode: 600
nsslapd-directory: /var/lib/dirsrv/slapd-localhost/db
nsslapd-db-logdirectory: /var/lib/dirsrv/slapd-localhost/db
nsslapd-db-home-directory
nsslapd-db-compactdb-interval: 2592000
nsslapd-db-locks: 10000
nsslapd-db-checkpoint-interval: 60
nsslapd-db-durable-transaction: on
nsslapd-db-transaction-wait: off
nsslapd-db-transaction-batch-val: 0
nsslapd-db-transaction-batch-min-wait: 50
nsslapd-db-transaction-batch-max-wait: 50
nsslapd-db-logbuf-size: 0
nsslapd-db-private-import-mem: on
nsslapd-idl-switch: new
nsslapd-search-bypass-filter-test: on
nsslapd-search-use-vlv-index: on
nsslapd-exclude-from-export:
nsslapd-serial-lock: on
nsslapd-subtree-rename-switch: on
nsslapd-backend-opt-level: 1



backend
=========================
nsslapd-cachesize: -1
nsslapd-cachememsize: 512000
nsslapd-readonly: off
nsslapd-require-index: off
nsslapd-directory: /var/lib/dirsrv/slapd-localhost/db/NetscapeRoot
nsslapd-dncachememsize: 16777216
nsslapd-state:  <from mapping tree entry>




*/


