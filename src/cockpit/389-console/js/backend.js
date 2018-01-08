

function customMenu (node) {
    var root_items = {
      "create_suffix": {
        "label": "Create Suffix",
        "icon": "glyphicon glyphicon-plus",
        "action": function (data) {
             // Create suffix
        }
      }
    };

   var suffix_items = {
     'import': {
          "label": "Initialize Suffix",
          "icon": "glyphicon glyphicon-circle-arrow-right",
         "action": function (data) {
           // Create suffix
          }
      },
      'export': {
          "label": "Export Suffix",
          "icon": "glyphicon glyphicon-circle-arrow-left",
         "action": function (data) {
           // Create suffix
          }
      },
      'reindex': {
          "label": "Reindex Suffix",
          "icon": "glyphicon glyphicon-wrench",
         "action": function (data) {
           // Create suffix
          }
      },
      "create_db_link": {
        "label": "Create Database Link",
        "icon": "glyphicon glyphicon-link",
        "action": function (data) {
          // Create suffix

         }
      },
      "create_sub_suffix": {
        "label": "Create Sub-Suffix",
        "icon": "glyphicon glyphicon-triangle-bottom",
        "action": function (data) {
          // Create suffix

         }
      },
      'delete_suffix': {
          "label": "Delete Suffix",
          "icon": "glyphicon glyphicon-remove",
         "action": function (data) {
           // Create suffix
          }
      }
   };

   if ( $(node).attr('id') == "root" ) {
       return root_items;
   } else {
        return suffix_items;
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
    console.log("The selected nodes are:");
    console.log(data.selected);
    var suffix = data.selected;
    if (suffix == "root"){
      $("#db").show();
       $("#suffix").hide();
    } else {
      $("#db").hide();
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
      }
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
    });
    $('#attr-encrypt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No Encrypted Attributes"
      },
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


