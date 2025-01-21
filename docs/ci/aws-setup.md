# AWS Setup for CI Testing

Our CI system needs to test the collector on various AWS instance types to validate hardware-specific features. **We use GitHub Actions to dynamically spin up EC2 instances with specific hardware configurations, run our tests, and then tear down the instances**.

## Overview

The CI system uses two key community actions:
- [machulav/EC2-github-runner](https://github.com/machulav/ec2-github-runner): Manages ephemeral EC2 instances for test execution
- [aws-actions/configure-aws-credentials](https://github.com/aws-actions/configure-aws-credentials): Handles AWS authentication through GitHub's OIDC provider

Each test workflow creates a dedicated GitHub Actions runner on a fresh EC2 instance. This ensures our tests run in clean environments with specific hardware configurations.

## AWS Account Setup

We maintain a dedicated AWS account for CI testing to isolate these resources from production environments. This separation provides clearer cost tracking and stronger security boundaries.

### Administrative Access

After creating the dedicated CI testing account, set up administrative access:

1. In the root account, navigate to IAM Identity Center
2. Create a new user in the IAM Identity Center
3. Create a Permission Set or use the existing "Administrator Access" permission set
   - Note: "Power User Access" is insufficient as it doesn't allow IAM role creation
4. Assign the user to the CI testing account with the Administrator Access permission set

**The Administrator Access permission set is required for subsequent IAM configuration steps**. While Power User Access might seem sufficient, it lacks the necessary permissions for creating IAM roles needed for GitHub Actions integration.

### IAM Configuration

First, configure GitHub as an OIDC provider to enable secure authentication:

1. Open the IAM console
2. Navigate to "Identity Providers" and add a new provider
3. Select "OpenID Connect"
4. Use `https://token.actions.githubusercontent.com` as the provider URL
5. Set the audience to `sts.amazonaws.com`

Next, create an IAM role for GitHub Actions:

1. Create a new role
2. Set the Trusted entity type to Web Identity
3. Select the GitHub OIDC provider as the trust entity
4. Fill in the org (`unvariance`) and repo (`collector`), not filling in the branch
5. Add no permissions (we will do this in a moment)
6. Name the role, e.g., `github-actions-collector`.
7. Verify the generated trusted entities to be:
```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Federated": "arn:aws:iam::<ACCOUNT-ID>:oidc-provider/token.actions.githubusercontent.com"
            },
            "Action": "sts:AssumeRoleWithWebIdentity",
            "Condition": {
                "StringEquals": {
                    "token.actions.githubusercontent.com:aud": "sts.amazonaws.com"
                },
                "StringLike": {
                    "token.actions.githubusercontent.com:sub": "repo:unvariance/collector:*"
                }
            }
        }
    ]
}
```

### EC2 Permissions

The IAM role needs permissions to manage EC2 instances and request Spot instances. Attach a policy with these minimum permissions:

```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "ec2:RunInstances",
                "ec2:TerminateInstances",
                "ec2:DescribeInstances",
                "ec2:DescribeInstanceStatus",
                "ec2:RequestSpotInstances",
                "ec2:CancelSpotInstanceRequests",
                "ec2:DescribeSpotInstanceRequests",
                "ec2:DescribeSpotPriceHistory"
            ],
            "Resource": "*"
        },
        {
            "Effect": "Allow",
            "Action": "ec2:CreateTags",
            "Resource": "*",
            "Condition": {
                "StringEquals": {
                    "ec2:CreateAction": [
                        "RunInstances",
                        "RequestSpotInstances"
                    ]
                }
            }
        }
    ]
}
```

## Network Configuration

Create a dedicated VPC for CI testing:

1. Create a new VPC with a single public subnet
2. Set up appropriate security groups to allow:
   - Outbound traffic on port 443 for communication with GitHub

## Repository variables

Configure the repository with the following secrets that can be used in Actions:

- `AWS_ROLE_ARN`: the ARN of the role that allows running and terminating instances
- `AWS_REGION`: the region where we'll run runners
- `AWS_SUBNET_ID`: the subnet ID, needs to be in `AWS_REGION`
- `AWS_SECURITY_GROUP_ID`: the name of the security group that allows runners to pull jobs
- `REPO_ADMIN_TOKEN`: see below

### Getting a token for ec2-github-runner

To register runners with GitHub, the `machulav/ec2-github-runner` action needs a GitHub token that has permissions to modify the the repository's set of self hosted runners. This might be transferable to user accounts but I haven't checked.

A [discussion thread](https://github.com/orgs/community/discussions/53361#discussioncomment-9289579) implies that finer-grained permissions might be available, where a token would only be able to configure runners rather than full Administration privileges, but it didn't work.

1. Configure your organization to allow fine-grained tokens. In Organization Settings -> Third-party Access -> Personal access tokens -> Settings, allow access via fine-grained personal access tokens
2. Create a fine-grained personal access token here: https://github.com/settings/personal-access-tokens/new
3. Set the resource owner to be the organization
4. Set the permission scope to "Only select repositories", and select the repo with the GitHub Action
4. In Repository permissions, add "Administration" (read and write)

## GitHub Workflow Configuration

For an example workflow, adapted from the [ec2-github-runner README](https://github.com/machulav/ec2-github-runner?tab=readme-ov-file#example) and [configure-aws-credentials README example](https://github.com/aws-actions/configure-aws-credentials?tab=readme-ov-file#assumerolewithwebidentity-recommended), see `/.github/workflows/aws-runner-template.yaml`.
