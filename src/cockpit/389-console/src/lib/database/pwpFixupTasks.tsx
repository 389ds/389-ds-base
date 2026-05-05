import cockpit from "cockpit";
import * as React from "react";
import {
  ChangeEvent,
  ComponentType,
  FunctionComponent,
  ReactNode,
  useState,
} from "react";
import { log_cmd, getApiErrorMessage } from "../tools.jsx";
import {
  Button,
  Text,
  TextContent,
  TextVariants,
} from "@patternfly/react-core";
import { LogViewer } from "@patternfly/react-log-viewer";
import { ShadowFixupModal as ShadowFixupModalUntyped } from "./databaseModal.jsx";
const _ = cockpit.gettext;

type ShadowFixupModalProps = {
  showModal: boolean;
  closeHandler: () => void;
  handleChange: (e: ChangeEvent<HTMLInputElement>) => void;
  saveHandler: () => void;
  spinning: boolean;
  item: ReactNode | null;
  force: boolean;
  suffix: string;
  fixupCompleted: boolean;
  suffixes: string[];
};

const ShadowFixupModal =
  ShadowFixupModalUntyped as unknown as ComponentType<ShadowFixupModalProps>;

export const PwpFixupTasks: FunctionComponent<{
  serverId: string;
  addNotification: (type: string, message: string) => void;
  suffixes: string[];
}> = ({ serverId, addNotification, suffixes }) => {
  const [modalSpinning, setModalSpinning] = useState(false);
  const [fixupShadowModalOpen, setFixupShadowModalOpen] = useState(false);
  const [fixupShadowSuffix, setFixupShadowSuffix] = useState("");
  const [fixupShadowForce, setFixupShadowForce] = useState(false);
  const [fixupCompleted, setFixupCompleted] = useState(false);
  const [fixupBuffer, setFixupBuffer] = useState("");

  const openFixUpModal = () => {
    setFixupShadowModalOpen(true);
    setFixupShadowSuffix("");
    setFixupShadowForce(false);
    setFixupCompleted(false);
    setFixupBuffer("");
  };

  const closeFixUpModal = () => {
    setFixupShadowModalOpen(false);
  };

  const handleFixUpChange = (e: ChangeEvent<HTMLInputElement>) => {
    const value =
      e.target.type === "checkbox" ? e.target.checked : e.target.value;
    const attr = e.target.name;
    if (attr === "fixupShadowForce") {
      setFixupShadowForce(value as unknown as boolean);
    } else {
      setFixupShadowSuffix(value as unknown as string);
    }
  };

  const doShadowFixup = () => {
    // Do Fix-Up task
    const fixup_cmd = [
      "dsconf",
      "-j",
      "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
      "pwpolicy",
      "fixup-shadow",
      fixupShadowSuffix,
      "--watch",
    ];

    if (fixupShadowForce) {
      fixup_cmd.push("--force");
    }

    setModalSpinning(true);
    setFixupBuffer("");
    let buffer = "";

    log_cmd("doShadowFixup", "Do shadow account fixup task", fixup_cmd);
    const proc = cockpit.spawn(fixup_cmd, {
      pty: true,
      superuser: "require",
      err: "message",
    });
    proc.stream((line: string) => {
      buffer += line;
      setFixupBuffer(buffer);
    });
    proc
      .done(() => {
        setModalSpinning(false);
        setFixupCompleted(true);
        addNotification(
          "success",
          _("Successfully ran shadow account fixup task"),
        );
      })
      .fail((err: Error) => {
        const errMsg = getApiErrorMessage(err);
        addNotification(
          "error",
          cockpit.format(_("Error running fixup task - $0"), errMsg),
        );
        setModalSpinning(false);
      });
  };

  let fixupItem: ReactNode | null = null;
  if (fixupBuffer !== "") {
    fixupItem = (
      <LogViewer
        data={fixupBuffer}
        isTextWrapped={false}
        hasLineNumbers={false}
        scrollToRow={fixupBuffer.length}
        height="200px"
      />
    );
  }

  return (
    <div>
      <TextContent>
        <Text component={TextVariants.h2}>
          {_("Password Policy Fix-up Tasks")}
        </Text>
      </TextContent>
      <hr />
      <TextContent className="ds-margin-top-xlg">
        <Text component={TextVariants.h3}>
          {_("Shadow Account Fix-up Task")}
        </Text>
        <Text className="ds-margin-top-lg ds-margin-left">
          Run a fix-up task to update users with the ShadowAccount objectclass.
          The task will add "ShadowLastChange" attribute to users that do not
          have that attribute.
        </Text>
      </TextContent>
      <Button
        variant="primary"
        className="ds-margin-top-lg ds-margin-left"
        onClick={openFixUpModal}
      >
        {_("Run Shadow Fix-Up Task")}
      </Button>
      <ShadowFixupModal
        showModal={fixupShadowModalOpen}
        closeHandler={closeFixUpModal}
        handleChange={handleFixUpChange}
        saveHandler={doShadowFixup}
        spinning={modalSpinning}
        item={fixupItem}
        force={fixupShadowForce}
        suffix={fixupShadowSuffix}
        fixupCompleted={fixupCompleted}
        suffixes={suffixes}
      />
    </div>
  );
};
