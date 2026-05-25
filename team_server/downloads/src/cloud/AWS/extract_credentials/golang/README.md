## Typical Appearance by Environment
1. AWS Cloud VM (EC2)
On a Linux or macOS Amazon EC2 instance, keys are commonly found in the AWS CLI configuration or exported as environment variables. 


 - Environment Variables:
```bash
AWS_ACCESS_KEY_ID=KEY-EXAMPLE
AWS_SECRET_ACCESS_KEY=SECRET_KEY_EXAMPLE
```
Or

 - Credentials File (located at ~/.aws/credentials):
```text 
ini
[default]
aws_access_key_id = KEY-EXAMPLE
aws_secret_access_key = SECRET_KEY_EXAMPLE
```

 - Compile the binary
 - go build -o aws_credentials_extractor main.go
 - Transfer it to the root of the downloads folder
 - plugin_download:aws_credentials_extractor
 - plugin_run:aws_credentials_extractor
 - cat ../on_demand_plugins/cloud_aws_extract_credentials.txt (THis will work only if you are running on AWS or else you can set a global AWS env variable on your system for testing)
