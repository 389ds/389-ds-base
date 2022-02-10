import React from "react";
import PropTypes from "prop-types";
import {
    Form,
    Grid,
    GridItem,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faLink,
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';

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
                            <FontAwesomeIcon size="sm" icon={faLink} /> {this.props.suffix} (<b>{this.props.bename}</b>) <FontAwesomeIcon
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title="Refresh chaining monitor"
                                onClick={() => this.props.reload(this.props.suffix)}
                            />
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg">
                        <GridItem span={3}>
                            Add Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsaddcount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            Delete Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsdeletecount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            Modify Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsmodifycount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            ModRDN Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsrenamecount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            Base Searches
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nssearchbasecount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            One-Level Searches
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nssearchonelevelcount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            Subtree Searches
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nssearchsubtreecount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            Abandon Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsabandoncount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            Bind Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsbindcount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            Unbind Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsunbindcount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            Compare Operations
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nscomparecount}</b>
                        </GridItem>
                        <GridItem span={3}>
                            Outgoing Connections
                        </GridItem>
                        <GridItem span={3}>
                            <b>{this.props.data.nsopenopconnectioncount}</b>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={3}>
                            Outgoing Bind Connections
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
