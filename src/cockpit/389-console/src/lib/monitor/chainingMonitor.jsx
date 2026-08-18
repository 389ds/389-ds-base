import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Button,
    Form,
    Grid,
    GridItem,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import {
    SyncAltIcon,
    LinkIcon
} from '@patternfly/react-icons';

const _ = cockpit.gettext;

export class ChainingMonitor extends React.Component {
    componentDidMount() {
        this.props.enableTree();
    }

    render() {
        return (
            <div id="monitor-server-page">
                <Form isHorizontal>
                    <TextContent>
                        <Text component={TextVariants.h2}>
                            <LinkIcon />
                            &nbsp;&nbsp;{this.props.suffix} (<i>{this.props.bename}</i>)
                            <Button 
                                variant="plain"
                                aria-label={_("Refresh chaining monitor")}
                                onClick={() => this.props.reload(this.props.suffix)}
                            >
                                <SyncAltIcon />
                            </Button>
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg">
                        <GridItem span={3}>
                            {_("Add Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsaddcount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Delete Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsdeletecount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            {_("Modify Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsmodifycount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("ModRDN Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsrenamecount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            {_("Base Searches")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nssearchbasecount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("One-Level Searches")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nssearchonelevelcount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            {_("Subtree Searches")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nssearchsubtreecount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Abandon Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsabandoncount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            {_("Bind Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsbindcount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Unbind Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsunbindcount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            {_("Compare Operations")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nscomparecount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Outgoing Connections")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsopenopconnectioncount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            {_("Outgoing Bind Connections")}
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsopenbindconnectioncount}</b>
                        </GridItem>
                    </Grid>
                </Form>
            </div>
        );
    }
}

ChainingMonitor.propTypes = {
    suffix: PropTypes.string,
    bename: PropTypes.string,
    data: PropTypes.object,
    enableTree: PropTypes.func,
};

ChainingMonitor.defaultProps = {
    suffix: "",
    bename: "",
    data: {},
};

export default ChainingMonitor;
