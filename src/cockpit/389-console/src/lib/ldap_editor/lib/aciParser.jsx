// CODE TAKEN FROM 389-ds Console
// ADAPTED TO JAVASCRIPT by tmihinto@redhat.com

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc.  Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version
 * 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 * END COPYRIGHT BLOCK **/

// TODO: Update these comments taken with 389-ds Console code!!!
/**
 * A utility class primarily to parse ACI strings
 * into <name><operator><value> triplets.
 * This class is used internally by ACIEditor and ACIManager.
 */

/**
     * Scans an aci String, breaks it up into ACIAttributes
     * which are <name><operator><value> triplets.
     *
     * @return Vector of ACIAttributes
     */
export function getAciAttributes (aci) {
  let startIndex = 0;
  const aciAttributes = [];
  const length = aci.length;
  for (let i = 0; i < length; i++) {
    switch (aci.charAt(i)) {
      case ' ': // skip over space
      case '\n': // skip over CR
        break;

      case '(':
        i++;
        // intentional drop through to default
      // eslint-disable-next-line no-fallthrough
      default:
        i = parseAciName(aci, i, aciAttributes, startIndex);
        startIndex = i;
        break;
    }
    if (i < 0) {
      i = -i;
      console.log('ACI ERROR: cannot parse at index ' + i);
      console.log(aci);
      let spaces = '';
      for (let x = 0; x < i; x++) {
        // console.log(' ');
        spaces += ' ';
      }
      // console.log('^');
      console.log(`${spaces}^`);
      break; // for loop
    }
  }
  return aciAttributes;
}

function parseAciName (aci, i, aciAttributes, startIndex) {
  const length = aci.length;
  let aciName = '';
  // <i> should be pointing to NAME
  // where <aci> is of the form: NAME<OPERATOR>VALUE

  let isNameFound = false;
  for (; i < length; i++) {
    const x = aci.charAt(i);
    switch (x) {
      case '\n': // skip over CR
        break;

      case '(': // skip over leading paren
      case ' ': // skip over leading space
        if (aciName.length > 0) { isNameFound = true; }
        break;

      case '|': // operators
      case '<':
      case '>':
      case '!':
      case '=':
        isNameFound = true;
        break;

      case ')':
        return i;

      case ';':
        break;

      default:
        aciName += x;
        break;
    }

    if (isNameFound) {
      if (aciName.length > 0) {
        const nameString = aciName.toString();
        if (nameString.toLowerCase() === 'and' || nameString.toLowerCase() === 'or') {
          //     public ACIAttribute(String name, String operator, String value, int startIndex, int endIndex)
          //                         ACIAttribute aciAttr = new ACIAttribute("", nameString, "", startIndex, i);

          const aciAttr = {
            aciName: '',
            operator: nameString,
            value: '',
            startIndex: startIndex,
            endIndex: i
          };
          aciAttributes.push(aciAttr);
          startIndex = i + 1;
        } else {
          if (aci.charAt(i) === ' ' || aci.charAt(i) === '(') { i++; }
          i = parseAciValue(aci, i, nameString, aciAttributes, startIndex);
          startIndex = i;
        }
        if (i > 0) { // no error, continue with next NAME/VALUE pair
          isNameFound = false;
          aciName = null;
          aciName = '';
          i--;
          continue; // for loop
        }
      }
      return i; // error, should be negative
    }
  }
  return i;
}

function parseAciValue (aci, i, aciName, aciAttributes, startIndex) {
  const length = aci.length;
  let value = '';
  let operator = '';
  // <i> should be pointing to OPERATOR
  // where <aci> is of the form: NAME<OPERATOR>VALUE

  let isValueFound = false;
  let isQuoteOpen = false;
  let countParenthesis = 0;
  for (; i < length; i++) {
    const x = aci.charAt(i);
    switch (x) {
      case '\n': // skip over CR
        break;

      case '|': //
      case '<': //
      case '>': // skip over operators
      case '!': //
      case '=': //
        if (value.length > 0) { value += x; } else { operator += x; }
        break;

      case ' ': // skip over leading space
        if (value.length > 0) { value += x; }
        break;

      case '"':
        value += x;
        isQuoteOpen = !isQuoteOpen;
        if (isQuoteOpen === false) {
          isValueFound = true;
        }
        break;

      case '(':
        if (isQuoteOpen === false) {
          countParenthesis++;
        }
        value += x;
        break;

      case ')':
        if ((countParenthesis > 0) || (isQuoteOpen)) {
          value += x;
        }
        if (!isQuoteOpen) {
          countParenthesis--;
          if (countParenthesis <= 0) {
            isValueFound = true;
          }
        }
        break;

      case ';':
        isValueFound = true;
        break;

      default:
        value += x;
        break;
    }

    if (isValueFound) {
      const len = value.length;
      if (len > 0) {
        let v = value.toString();
        if (v.endsWith('"')) {
          v = v.substring(0, v.length - 1);
        }

        if (v.startsWith('"')) {
          v = v.substring(1);
        }
        //     public ACIAttribute(String name, String operator, String value, int startIndex, int endIndex)
        // ACIAttribute aciAttr = new ACIAttribute(aciName, operator.toString(), v, startIndex, i);
        // aciAttr = new ACIAttribute(aciName, operator.toString(), v, startIndex, i);
        const aciAttr = {
          aciName: aciName,
          operator: operator.toString(),
          value: v,
          startIndex: startIndex,
          endIndex: i
        };

        aciAttributes.push(aciAttr);
        return i + 1;
      }
      break; // exit for loop, error
    }
  }
  return -i; // error
}

export function checkAcis () {
  // String testaci = "(targetattr=\"*\")(version 3.0; acl \"Enable Group Expansion\"; allow (read, search, compare) groupdnattr=\"ldap:///o=NetscapeRoot?uniquemember?sub\";)";
  // String testaci = "(targetattr=\"*\")(ERROR=)(version 3.0; acl \"Enable Group Expansion\"; allow (read, search, compare) groupdnattr=\"ldap:///o=NetscapeRoot?uniquemember?sub\";)";
  // String testaci = "(targetattr=\"*\")(targetfilter=(|(objectClass=nsManagedDomain)(|(objectClass=nsManagedOrgUnit)(|(objectClass=nsManagedDept)(|(objectClass=nsManagedMailList)(objectClass=nsManagedPerson))))))(version 3.0; acl \"SA domain access\"; allow (all) groupdn=\"ldap:///cn=Service Administrators, o=NDA Spock 1222\";)";
  // String testaci = "(targetattr!=\"uid||ou||owner||nsDAModifiableBy||nsDACapability||mail||mailAlternateAddress||nsDAMemberOf||nsDADomain\")(targetfilter=(objectClass=nsManagedPerson))(version 3.0; acl \"User self modification\"; allow (write) userdn=\"ldap:///self\";)";
  // String testaci = "(target=\"ldap:///cn=postmaster, o=NDA Spock 1222\")(targetattr=\"*\")(version 3.0; acl \"Anonymous access to Postmaster entry\"; allow (read,search) userdn=\"ldap:///anyone\";)";
  // String testaci = "(targetattr=\"*\")(targetfilter=(objectClass=nsManagedDept))(version 3.0; acl \"Dept Adm dept access\"; allow (read,search) userdn=\"ldap:///o=NDA Spock 1222??sub?(nsDAMemberOf=cn=Department Administrators*)\" and groupdnattr=\"ldap:///o=NDA Spock 1222?nsDAModifiableBy\";)";
  // String testaci = "(targetattr!=\"uid||ou||owner||nsDAModifiableBy||nsDACapability||mail||mailAlternateAddress||nsDAMemberOf||nsDADomain\")(targetfilter=(objectClass=nsManagedPerson))(version 3.0; acl \"User self modification\"; allow (write) (userdn=\"ldap:///self\" or userdn=\"ldap:///self\") ;)";
  // String testaci = "(targetattr!=\"*\")(version 3.0; acl \"aclname\"; allow (all) (userdn=\"ldap:///self\" or userdn=\"ldap:///self\") ;)";
  const testaci = '(targetattr = "*") (version 3.0; acl "<Unnamed ACI>"; allow (all) (userdn = "ldap:///anyone") and (dns="*.mcom.com");)';
  // aci: (targetattr="dc || description || objectClass")(targetfilter="(objectClass=domain)")(version 3.0; acl "Enable anyone domain read"; allow (read, search, compare)(userdn="ldap:///anyone");)

  console.log('aci: ' + testaci);
  const aciData = getAciAttributes(testaci);
  aciData.map(datum => {
    console.log(datum);
  });
  const finalAci = aciData.filter(aci => aci.aciName === 'acl');
  finalAci.map(aci => {
    console.log('\nThe name of this ACI is "' + aci.value + '"');
  });
}

export function getAciActualName (fullAci) {
  console.log('fullAci = ' + fullAci);
  const aciData = getAciAttributes(fullAci);
  aciData.map(datum => {
    console.log(datum);
  });
  const aciName = aciData.filter(aci => aci.aciName === 'acl');
  const aciActualName = aciName[0].value;
  console.log(`aciActualName = ${aciActualName}`);
  return aciActualName;
}
