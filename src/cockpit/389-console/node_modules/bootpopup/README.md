# BootPopup

Popup dialog boxes for Bootstrap.

See it in action in [BootPopup - Examples](http://www.bootpopup.tk/#examples)


## Content

- [API](#api)
  - [bootpopup.alert](#bootpopupalertmessage-title-callback)
  - [bootpopup.confirm](#bootpopupconfirmmessage-title-callback)
  - [bootpopup.prompt](#bootpopuppromptlabel-type-message-title-callback) (single value)
  - [bootpopup.prompt](#bootpopuppromptlist_types-message-title-callback) (multiple values)
  - [bootpopup](#bootpopupoptions)
    - [About the buttons option](#about-the-buttons-option)
    - [About the content option](#about-the-content-option)
  - [bootpopup object](#bootpopup-object)
    - [Properties](#properties)
    - [Methods](#methods)
    - [DOM Elements](#dom-elements)
- [Examples](#examples)
- [Migration from previous version to v1](#migration-from-previous-version-to-v1)

## API

### `bootpopup.alert(message[, title[, callback]])`
  
Shows an alert dialog box.
**Return:** instance of BootPopup window

- **message**:
  - `(string)` message of the alert
- **title**:
  - `(string)` title of the alert. Default value is page title
  - `(function)()` callback when the alert is dismissed
- **callback**:
  - `(function)()` callback when the alert is dismissed


### `bootpopup.confirm(message[, title[, callback]])`

Shows a confirm dialog box.
**Return:** instance of BootPopup window

- **message**:
  - `(string)` message to confirm
- **title**:
  - `(string)` title of the confirm dialog. Default value is page title
  - `(function)(answer)` callback when the confirm is answered. `answer` will be `true` if the answer was yes and `false` if it was no. If dismissed, the default answer is no
- **callback**:
  - `(function)(answer)` callback when the confirm is answered. `answer` will be `true` if the answer was yes and `false` if it was no. If dismissed, the default answer is no


### `bootpopup.prompt(label[, type[, message[, title[, callback]]]])`

Shows a prompt dialog box, asking to input a single value.
**Return:** instance of BootPopup window

- **label**:
  - `(string)` label of the value being asked
- **type**:
  - `(string)` type of the value being asked. This corresponds to the [HTML input types](http://www.w3schools.com/tags/att_input_type.asp). Default value is `text`
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed
- **message**:
  - `(string)` message shown before the asked value. Default value is *Provide a `type` for:*
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed
- **title**:
  - `(string)` title of the prompt dialog. Default value is page title
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed
- **callback**:
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed


### `bootpopup.prompt(list_types[, message[, title[, callback]]])`

Shows a prompt dialog box, asking to input multiple values.
**Return:** instance of BootPopup window

- **list_types**:
  - `(string)` array of objects with the description of values being asked:
    - **label** label of the value
    - **type** type of the value (default is `text`)
    - **name** key used in the data returned to the callback (default is label in lowercase and dashed)
    - <a href="https://www.w3schools.com/html/html_form_attributes.asp">HTML input attributes</a> are also accepted. Example:
    `{ label: "Name", type: "text", name: "name", value: "My name"}`
- **message**:
  - `(string)` message shown before the asked value. Default value is *Provide a `type` for:*
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed
- **title**:
  - `(string)` title of the prompt dialog. Default value is page title
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed
- **callback**:
  - `(function)(answer)` callback with the introduced data. This is only called when OK is pressed


### `bootpopup(options)`

Shows a customized dialog box. `bootpopup.alert`, `bootpopup.confirm` and `bootpopup.prompt` are mapped into this function.
**Return:** instance of BootPopup window

**Options:** `(object)`

| Name        | Type     | Default          | Example             | Description
|-------------|----------|------------------|---------------------|------------
| title       | string   | `document.title` | `"A title"`         | Title of the dialog box
| showclose   | boolean  | `true`           | `false`             | Show or not the close button in the title
| content     | array    | `[]`             | `[ {p}, {p} ]`      | Content of the dialog box. Learn more [about the content option](#about-the-content-option)
| size        | string   | `normal`         | `large`             | Size of the modal window. Values accepted: `small`, `normal`, `large` ([Bootstrap Modal optional sizes](https://getbootstrap.com/docs/3.3/javascript/#modals-sizes))
| size_labels | string   | `col-sm-4`       | `col-lg-2`          | Any class name or list of classes to apply to labels in the form. Preferably classes from [Bootstrap Grid system](https://getbootstrap.com/docs/3.3/css/#grid)
| size_inputs | string   | `col-sm-8`       | `col-lg-10`         | Any class name or list of classes to apply to inputs (div that wraps the input) in the form. Preferably classes from [Bootstrap Grid system](https://getbootstrap.com/docs/3.3/css/#grid)
| onsubmit    | string   | `close`          | `ok`                | Default action to be executed when the form is sumitted. This is overrided if you define a callback for `submit`. The possible options are: `close`, `ok`, `cancel`, `yes`, `no`.
| buttons     | array    | `["close"]`      | `[ "yes", "no"]`    | List of buttons to show in the bottom of the dialog box. The possible options are: `close`, `ok`, `cancel`, `yes`, `no`. Learn more [about the buttons option](#about-the-buttons-option)
| before      | function | `function() {}`  | `function(diag) {}` | Called before the window is shown, but after being created. `diag` provides the instance to `bootpopup` object
| dismiss     | function | `function() {}`  | `function(data) {}` | Called when the window is dismissed
| submit      | function | `function() {}`  | `function(data) {}` | Called when the form is submitted. Returning `false` will cancel submission
| close       | function | `function() {}`  | `function(data) {}` | Called when Close button is selected
| ok          | function | `function() {}`  | `function(data) {}` | Called when OK button is selected
| cancel      | function | `function() {}`  | `function(data) {}` | Called when Cancel button is selected
| yes         | function | `function() {}`  | `function(data) {}` | Called when Yes button is selected
| no          | function | `function() {}`  | `function(data) {}` | Called when No button is selected
| complete    | function | `function() {}`  | `function(data) {}` | This function is always called when the dialog box has completed

#### About the **buttons** option:

If `buttons` is not specified, BootPopup will automatically select the buttons based on the defined callbacks.
If some of the callbacks `close`, `ok`, `cancel`, `yes`, `no` are defined, the respective buttons are selected.
  
For example, if you define `ok` and `cancel` callbacks, the option `buttons` is automatically configured to
`["ok", "cancel"]`.


#### About the **content** option:

The biggest flexibility of BootPopup is the `content` option. The content is wrapped by a form and has the
bootstrap class `.form-horizontal` allowing to create complex forms very quickly. When you are submitting data
via a dialog box, BootPopup will grab all that data and deliver to you through the callbacks.

1. `content` is an array of objects and each object is represented as an entry of the form. For example, if you
   have the following object:
   
   ```javascript
   { p: {class: "bold", text: "Insert data:"}}
   ```
   
   This will add a `<p></p>` tag to the form. The options of `p` (`{class: "bold", text: "Insert data:"}`) are HTML
   attributes passed to the HTML tag. There is a special attribute for `text` which is defined as the inner text of
   the HTML tag. So, this example is equivalent to the following HTML:
   
   ```html
   <p class="bold">Insert data:</p>
   ```

2. But it is when it comes to adding inputs that things become easy. Look at this example:
   
   ```javascript
   { input: {type: "text", label: "Title", name: "title", placeholder: "Description" }}
   ```
   
   This will create an `input` element with the attributes `type: "text", label: "Title", name: "title", placeholder: "Description"`.
   Note there is also a special attribute `label`. This attribute is used by BootPopup to create a label for the input form entry.
   The above example is equivalent to the following HTML:
   
   ```html
   <div class="form-group">
     <label for="title" class="col-sm-2 control-label">Title</label>
     <div class="col-sm-10">
       <input label="Title" name="title" id="bootpopup-form-input" placeholder="Description" class="form-control" type="text">
     </div>
   </div>
   ```
   
3. In order to make it even simpler, there are shortcuts for most common input types (`button`, `text`, `submit`, `color`,
   `url`, `password`, `hidden`, `file`, `number`, `email`, `reset`, `date`).
   The previous example can be simply written as:
   
   ```javascript
   { text: {label: "Title", name: "title", placeholder: "Description" }}
   ```

4. Another useful feature is the ability to support functions directly as an attribute. Take the following `button` example:
   
   ```javascript
   { button: {name: "button", value: "Open image", class: "btn btn-info", onclick: function(obj) {
       console.log(obj);
       bootpopup.alert("Hi there");
   }}}
   ```
   This will create a `onclick` event for the button. The reference for the object is passed as argument to the function.

5. You can also insert HTML strings directly. Instead of writing an JS object, write the HTML:

   ```javascript
   '<p class="lead">Popup dialog boxes for Bootstrap.</p>'
   ```
### `bootpopup` object

The `bootpopup` object is returned every time a new instance of BootPopup is created.

#### Properties

- `formid` - HTML ID of the form, this is a randomly generated
- `options` - list of options used to create the window

#### Methods

 - `addOptions` - add options to the current options
 - `setOptions` - override the current options, a list with all options is required
 - `create` - create the window and add it to DOM, but not show
 - `show` - show window and call `before` callback
 - `dismiss` - performs a `dismiss`
 - `submit` - performs a `submit`
 - `close` - performs a `close`
 - `ok` - performs a `ok`
 - `cancel` - performs a `cancel`
 - `yes` - performs a `yes`
 - `no` - performs a `no`

#### DOM elements

All the following BootPopup properties are jQuery objects:

- `modal` - entire window, including the fade background. You can use this property in the same way as described in [Bootstrap Modals Usage](https://getbootstrap.com/docs/3.3/javascript/#modals-usage)
- `dialog` - entire window, without the background
- `content` - content of the dialog
- `header` - header of the dialog
- `body` - body of the dialog
- `form` - main form in the dialog, inside the `body`
- `footer` - footer of the dialog
- `btnClose` - close button (if present)
- `btnOk` - OK button (if present)
- `btnCancel` - cancel button (if present)
- `btnYes` - yes button (if present)
- `btnNo` - no button (if present)


## Examples

Open `index.html` to see the library in action.

- Alert:

  ```javascript
  bootpopup.alert("Hi there");
  ```

- Confirm:

  ```javascript
  bootpopup.confirm("Do you confirm this message?", function(ans) {
    alert(ans);
  });
  ```

- Prompt:

  ```javascript		
  bootpopup.prompt("Name", function(value) {
    alert(value);
  });
  ```

- Customized prompt:

  ```javascript
  bootpopup({
      title: "Add image",
      content: [
          '<p class="lead">Add an image</p>',
          { p: {text: "Insert image info here:"}},
          { input: {type: "text", label: "Title", name: "title", placeholder: "Description for image"}},
          { input: {type: "text", label: "Link", name: "link", placeholder: "Hyperlink for image"}}],
      cancel: function(data) { alert("Cancel"); },
      ok: function(data,e) { console.log(data,e); },
      complete: function() { alert("complete"); },
  });
  ```


## Migration from previous version to v1

- The value passed in the argument to the `prompt` callback is now the actual value
- The parameters passed to the callback of `bootpopup` are now:
    1. `data` - a list of key-value pairs of the form, where key is the name of the input
    2. `array` - an array of name-value pairs obtained from the jQuery function `$(form).serializeArray()`
    3. `event` - event of pressing the button
