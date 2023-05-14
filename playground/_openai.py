import sys
print(sys.version_info)
import openai
openai.organization = "org-y2lj7qm9IJP7hwZhW67E5YSk"
openai.api_key = "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"

completion = openai.ChatCompletion.create(
  model="gpt-3.5-turbo",
  messages=[
    {"role": "user", "content": "calculate 1+1"},
    {"content": "2", "role": "assistant"},
    {"role": "user", "content": "add 5"},
  ]
)

print(completion.choices[0].message)
